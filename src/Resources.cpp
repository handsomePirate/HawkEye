#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/ErrorCheck.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <vulkan/vulkan.hpp>

struct HawkEye::HTexture_t
{
	VulkanBackend::Image image;
	VkImageView imageView = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkFence uploadFence = VK_NULL_HANDLE;
	VkCommandBuffer transferCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer generalCommandBuffer = VK_NULL_HANDLE;
	VulkanBackend::Buffer stagingBuffer{};
	VkSemaphore mipsSemaphore = VK_NULL_HANDLE;
};

int GetMipCount(int width, int height)
{
	int largerSize = (width > height) ? width : height;

	return (int)(std::floor(std::log2(largerSize))) + 1;
}

VkFormat TranslateFormat(HawkEye::TextureFormat textureFormat, HawkEye::ColorCompression colorCompression)
{
	// Beware, RGB format may not be supported on the GPU.
	if (colorCompression == HawkEye::ColorCompression::SRGB)
	{
		switch (textureFormat)
		{
		case HawkEye::TextureFormat::Gray:
			return VK_FORMAT_R8_SRGB;
		case HawkEye::TextureFormat::GrayAlpha:
			return VK_FORMAT_R8G8_SRGB;
		case HawkEye::TextureFormat::RGB:
			return VK_FORMAT_R8G8B8_SRGB;
		case HawkEye::TextureFormat::RGBA:
			return VK_FORMAT_R8G8B8A8_SRGB;
		}
	}
	else
	{
		switch (textureFormat)
		{
		case HawkEye::TextureFormat::Gray:
			return VK_FORMAT_R8_UNORM;
		case HawkEye::TextureFormat::GrayAlpha:
			return VK_FORMAT_R8G8_UNORM;
		case HawkEye::TextureFormat::RGB:
			return VK_FORMAT_R8G8B8_UNORM;
		case HawkEye::TextureFormat::RGBA:
			return VK_FORMAT_R8G8B8A8_UNORM;
		}
	}
	return VK_FORMAT_UNDEFINED;
}

HawkEye::HTexture HawkEye::UploadTexture(RendererData rendererData, unsigned char* data, int dataSize, int width, int height,
	TextureFormat format, ColorCompression colorCompression, TextureCompression textureCompression,
	bool generateMips, TextureUsage usage)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;
	VkDevice device = backendData.logicalDevice;
	const int mipCount = generateMips ? GetMipCount(width, height) : 1;

	HTexture_t* result = new HTexture_t;
	VkFormat imageFormat = TranslateFormat(format, colorCompression);
	
	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		(generateMips ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
	result->image = VulkanBackend::CreateImage2D(backendData, width, height, 1, mipCount,
		imageUsage, imageFormat, VMA_MEMORY_USAGE_GPU_ONLY);

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = mipCount;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	result->imageView = VulkanBackend::CreateImageView2D(backendData, result->image.image, imageFormat, subresourceRange);
	
	result->sampler = VulkanBackend::CreateImageSampler(backendData, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
		VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.f, (float)mipCount);

	result->transferCommandBuffer = VulkanBackend::AllocateCommandBuffer(backendData, backendData.transferCommandPool);
	result->generalCommandBuffer = generateMips ? VulkanBackend::AllocateCommandBuffer(backendData, backendData.generalCommandPool) : VK_NULL_HANDLE;
	result->mipsSemaphore = generateMips ? VulkanBackend::CreateSemaphore(backendData) : VK_NULL_HANDLE;

	// Create staging command buffer.
	result->stagingBuffer = VulkanBackend::CreateBuffer(backendData, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		dataSize, VMA_MEMORY_USAGE_CPU_ONLY);

	// Copy data to staging buffer.
	{
		void* stagingBufferData;
		VulkanCheck(vmaMapMemory(backendData.allocator, result->stagingBuffer.allocation, &stagingBufferData));
		memcpy(stagingBufferData, data, dataSize);
		vmaUnmapMemory(backendData.allocator, result->stagingBuffer.allocation);
	}

	result->uploadFence = VulkanBackend::CreateFence(backendData);

	// All in a single command buffer.
	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VulkanCheck(vkBeginCommandBuffer(result->transferCommandBuffer, &commandBufferBeginInfo));
	
	// Transition layout to dst optimal.
	VulkanBackend::TransitionImageLayout(result->transferCommandBuffer, result->imageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		result->image.image, mipCount, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);
	result->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	// Copy from staging buffer to GPU.
	VulkanBackend::CopyBufferToImage(backendData, result->stagingBuffer.buffer, result->image.image, result->imageLayout,
		result->transferCommandBuffer, width, height, VK_IMAGE_ASPECT_COLOR_BIT);
	
	// Transition layout shader read only optimal.
	if (generateMips)
	{
		VulkanBackend::TransitionImageLayout(result->transferCommandBuffer, result->imageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			result->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, 0, backendData.transferFamilyIndex, backendData.generalFamilyIndex);
	}
	else
	{
		int nextQueueIndex = usage == TextureUsage::General ? backendData.generalFamilyIndex : backendData.computeFamilyIndex;
		VulkanBackend::TransitionImageLayout(result->transferCommandBuffer, result->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			result->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, 0, backendData.transferFamilyIndex, nextQueueIndex);
		result->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VulkanCheck(vkEndCommandBuffer(result->transferCommandBuffer));

	// Queue submit.
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &result->transferCommandBuffer;

	if (generateMips)
	{
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &result->mipsSemaphore;
		VulkanCheck(vkQueueSubmit(backendData.transferQueues[0], 1, &submitInfo, nullptr));
	}
	else
	{
		VulkanCheck(vkQueueSubmit(backendData.transferQueues[0], 1, &submitInfo, result->uploadFence));
	}

	if (generateMips)
	{
		VkCommandBufferBeginInfo commandBufferBeginInfo{};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VulkanCheck(vkBeginCommandBuffer(result->generalCommandBuffer, &commandBufferBeginInfo));

		VulkanBackend::GenerateMips(backendData, result->generalCommandBuffer, result->image.image, imageFormat, width, height, mipCount);
		result->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		int srcQueueIndex = usage == TextureUsage::General ? VK_QUEUE_FAMILY_IGNORED : backendData.generalFamilyIndex;
		int dstQueueIndex = usage == TextureUsage::General ? VK_QUEUE_FAMILY_IGNORED : backendData.computeFamilyIndex;
		VulkanBackend::TransitionImageLayout(result->generalCommandBuffer, result->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			result->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, 0, srcQueueIndex, dstQueueIndex);
		result->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VulkanCheck(vkEndCommandBuffer(result->generalCommandBuffer));

		submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &result->generalCommandBuffer;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &result->mipsSemaphore;
		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		submitInfo.pWaitDstStageMask = &waitStageMask;

		VulkanCheck(vkQueueSubmit(backendData.generalQueues[0], 1, &submitInfo, result->uploadFence));
	}

	return result;
}

void HawkEye::DeleteTexture(RendererData rendererData, HTexture& texture)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;
	
	vkWaitForFences(backendData.logicalDevice, 1, &texture->uploadFence, VK_FALSE, UINT64_MAX);
	vkResetFences(backendData.logicalDevice, 1, &texture->uploadFence);

	VulkanBackend::FreeCommandBuffer(backendData, backendData.transferCommandPool, texture->transferCommandBuffer);
	if (texture->generalCommandBuffer)
	{
		VulkanBackend::FreeCommandBuffer(backendData, backendData.generalCommandPool, texture->generalCommandBuffer);
		VulkanBackend::DestroySemaphore(backendData, texture->mipsSemaphore);
	}
	VulkanBackend::DestroyFence(backendData, texture->uploadFence);
	VulkanBackend::DestroyBuffer(backendData, texture->stagingBuffer);
	VulkanBackend::DestroyImageSampler(backendData, texture->sampler);
	VulkanBackend::DestroyImageView(backendData, texture->imageView);
	VulkanBackend::DestroyImage(backendData, texture->image);

	delete texture;

	texture = nullptr;
}
