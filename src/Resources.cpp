#include "HawkEye/HawkEyeAPI.hpp"
#include "Resources.hpp"
#include <SoftwareCore/DefaultLogger.hpp>
#include <VulkanBackend/ErrorCheck.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <vulkan/vulkan.hpp>
#include <math.h>

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

HawkEye::HTexture HawkEye::UploadTexture(HRendererData rendererData, void* data, int dataSize, int width, int height,
	TextureFormat format, ColorCompression colorCompression, TextureCompression textureCompression,
	bool generateMips, TextureQueue usage)
{
	// NOTE: Currently includes ownership acquisition.
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;
	VkDevice device = backendData.logicalDevice;
	const int mipCount = generateMips ? GetMipCount(width, height) : 1;

	HTexture texture = new HTexture_t;
	VkFormat imageFormat = TranslateFormat(format, colorCompression);

	texture->mipCount = mipCount;
	texture->currentUsage = usage;

	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		(generateMips ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
	texture->image = VulkanBackend::CreateImage2D(backendData, width, height, 1, mipCount,
		imageUsage, imageFormat, VMA_MEMORY_USAGE_GPU_ONLY);

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = mipCount;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	texture->imageView = VulkanBackend::CreateImageView2D(backendData, texture->image.image, imageFormat, subresourceRange);

	texture->sampler = VulkanBackend::CreateImageSampler(backendData, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
		VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.f, (float)mipCount);

	// Create staging command buffer.
	texture->stagingBuffer = VulkanBackend::CreateBuffer(backendData, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		dataSize, VMA_MEMORY_USAGE_CPU_ONLY);

	// Copy data to staging buffer.
	{
		void* stagingBufferData;
		VulkanCheck(vmaMapMemory(backendData.allocator, texture->stagingBuffer.allocation, &stagingBufferData));
		memcpy(stagingBufferData, data, dataSize);
		vmaUnmapMemory(backendData.allocator, texture->stagingBuffer.allocation);
	}

	texture->uploadFence = VulkanBackend::CreateFence(backendData);

	texture->generalCommandBuffer = VulkanBackend::AllocateCommandBuffer(backendData, backendData.generalCommandPool);
	texture->operationSemaphore = VulkanBackend::CreateSemaphore(backendData);
	texture->uploadSemaphore = VulkanBackend::CreateSemaphore(backendData);

	// All in a single command buffer.
	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VulkanCheck(vkBeginCommandBuffer(texture->generalCommandBuffer, &commandBufferBeginInfo));

	// Transition layout to dst optimal.
	VulkanBackend::TransitionImageLayout(texture->generalCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		texture->image.image, mipCount, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);
	texture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	// Copy from staging buffer to GPU.
	VulkanBackend::CopyBufferToImage(backendData, texture->stagingBuffer.buffer, texture->image.image, texture->imageLayout,
		texture->generalCommandBuffer, width, height, VK_IMAGE_ASPECT_COLOR_BIT);

	// Transition layout shader read only optimal.
	if (!generateMips)
	{
		VulkanBackend::TransitionImageLayout(texture->generalCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			texture->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, 0);
	}
	else
	{
		VulkanBackend::GenerateMips(backendData, texture->generalCommandBuffer, texture->image.image, imageFormat, width, height, mipCount);
		texture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		VulkanBackend::TransitionImageLayout(texture->generalCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			texture->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, 0);
	}

	texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texture->currentFamilyIndex = backendData.generalFamilyIndex;

	VulkanCheck(vkEndCommandBuffer(texture->generalCommandBuffer));

	// Queue submit.
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &texture->generalCommandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &texture->operationSemaphore;
	VulkanCheck(vkQueueSubmit(backendData.generalQueues[1], 1, &submitInfo, texture->uploadFence));

	return texture;
}

void HawkEye::DeleteTexture(HRendererData rendererData, HTexture& texture)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	WaitForUpload(rendererData, texture);

	VulkanBackend::FreeCommandBuffer(backendData, backendData.generalCommandPool, texture->generalCommandBuffer);
	VulkanBackend::DestroySemaphore(backendData, texture->operationSemaphore);
	VulkanBackend::DestroySemaphore(backendData, texture->uploadSemaphore);
	VulkanBackend::DestroyFence(backendData, texture->uploadFence);
	VulkanBackend::DestroyBuffer(backendData, texture->stagingBuffer);
	VulkanBackend::DestroyImageSampler(backendData, texture->sampler);
	VulkanBackend::DestroyImageView(backendData, texture->imageView);
	VulkanBackend::DestroyImage(backendData, texture->image);

	delete texture;

	texture = nullptr;
}

void HawkEye::WaitForUpload(HRendererData rendererData, HTexture texture)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	VulkanCheck(vkWaitForFences(backendData.logicalDevice, 1, &texture->uploadFence, VK_FALSE, UINT64_MAX));
}

bool HawkEye::UploadFinished(HRendererData rendererData, HTexture texture)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	VkResult status = vkGetFenceStatus(backendData.logicalDevice, texture->uploadFence);
	return status == VK_SUCCESS;
}

HawkEye::HBuffer HawkEye::UploadBuffer(HRendererData rendererData, void* data, int dataSize, BufferUsage usage,
	BufferType type, BufferQueue bufferQueue)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	HBuffer buffer = new HBuffer_t;
	VkBufferUsageFlags bufferUsage{};
	switch (usage)
	{
	case HawkEye::BufferUsage::Vertex:
		bufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		break;
	case HawkEye::BufferUsage::Index:
		bufferUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		break;
	case HawkEye::BufferUsage::Uniform:
		bufferUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		break;
	case HawkEye::BufferUsage::Storage:
		bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		break;
	default:
		break;
	}

	buffer->dataSize = dataSize;

	if (type == BufferType::Mapped)
	{
		buffer->buffer = VulkanBackend::CreateBuffer(backendData, bufferUsage, dataSize, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VulkanCheck(vmaMapMemory(backendData.allocator, buffer->buffer.allocation, &buffer->mappedBuffer));
		if (data)
		{
			memcpy(buffer->mappedBuffer, data, dataSize);
		}

		return buffer;
	}

	buffer->buffer = VulkanBackend::CreateBuffer(backendData, VK_BUFFER_USAGE_TRANSFER_DST_BIT | bufferUsage, dataSize,
		VMA_MEMORY_USAGE_GPU_ONLY);
	buffer->stagingBuffer = VulkanBackend::CreateBuffer(backendData, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		dataSize, VMA_MEMORY_USAGE_CPU_ONLY);

	// Copy data to staging buffer.
	if (data)
	{
		void* stagingBufferData;
		VulkanCheck(vmaMapMemory(backendData.allocator, buffer->stagingBuffer.allocation, &stagingBufferData));
		memcpy(stagingBufferData, data, dataSize);
		vmaUnmapMemory(backendData.allocator, buffer->stagingBuffer.allocation);

		buffer->uploadFence = VulkanBackend::CreateFence(backendData);

		buffer->generalCommandBuffer = VulkanBackend::AllocateCommandBuffer(backendData, backendData.generalCommandPool);

		// All in a single command buffer.
		VkCommandBufferBeginInfo commandBufferBeginInfo{};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VulkanCheck(vkBeginCommandBuffer(buffer->generalCommandBuffer, &commandBufferBeginInfo));

		VulkanBackend::CopyBufferToBuffer(backendData, buffer->stagingBuffer.buffer, buffer->buffer.buffer, dataSize,
			buffer->generalCommandBuffer);

		buffer->currentFamilyIndex = backendData.generalFamilyIndex;

		VulkanCheck(vkEndCommandBuffer(buffer->generalCommandBuffer));

		// Queue submit.
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &buffer->generalCommandBuffer;

		VulkanCheck(vkQueueSubmit(backendData.generalQueues[1], 1, &submitInfo, buffer->uploadFence));
	}
	else
	{
		buffer->uploadFence = VulkanBackend::CreateFence(backendData, VK_FENCE_CREATE_SIGNALED_BIT);
		buffer->currentFamilyIndex = bufferQueue == BufferQueue::General ? backendData.generalFamilyIndex :
			backendData.computeFamilyIndex;
	}

	return buffer;
}

void HawkEye::DeleteBuffer(HRendererData rendererData, HBuffer& buffer)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	WaitForUpload(rendererData, buffer);

	if (buffer->uploadFence != VK_NULL_HANDLE)
	{
		VulkanBackend::FreeCommandBuffer(backendData, backendData.generalCommandPool, buffer->generalCommandBuffer);
		VulkanBackend::DestroyFence(backendData, buffer->uploadFence);
		VulkanBackend::DestroyBuffer(backendData, buffer->stagingBuffer);
	}
	else
	{
		vmaUnmapMemory(backendData.allocator, buffer->buffer.allocation);
		buffer->mappedBuffer = nullptr;
	}
	VulkanBackend::DestroyBuffer(backendData, buffer->buffer);

	delete buffer;

	buffer = nullptr;
}

void HawkEye::UpdateBuffer(HRendererData rendererData, HBuffer buffer, void* data, int dataSize)
{
	if (dataSize > buffer->dataSize)
	{
		CoreLogError(DefaultLogger, "Data upload: Specified data size is bigger than buffer size.");
		return;
	}

	if (buffer->mappedBuffer)
	{
		memcpy(buffer->mappedBuffer, data, dataSize);
	}
	else
	{
		// TODO: Probably memory barrier.
		const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

		WaitForUpload(rendererData, buffer);
		vkResetFences(backendData.logicalDevice, 1, &buffer->uploadFence);

		{
			void* stagingBufferData;
			VulkanCheck(vmaMapMemory(backendData.allocator, buffer->stagingBuffer.allocation, &stagingBufferData));
			memcpy(stagingBufferData, data, dataSize);
			vmaUnmapMemory(backendData.allocator, buffer->stagingBuffer.allocation);
		}

		// Queue submit.
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &buffer->generalCommandBuffer;

		VulkanCheck(vkQueueSubmit(backendData.generalQueues[0], 1, &submitInfo, buffer->uploadFence));
	}
}

void HawkEye::WaitForUpload(HRendererData rendererData, HBuffer buffer)
{
	if (buffer->uploadFence != VK_NULL_HANDLE)
	{
		const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

		VulkanCheck(vkWaitForFences(backendData.logicalDevice, 1, &buffer->uploadFence, VK_FALSE, UINT64_MAX));
	}
}

bool HawkEye::UploadFinished(HRendererData rendererData, HBuffer buffer)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	VkResult status = vkGetFenceStatus(backendData.logicalDevice, buffer->uploadFence);
	return status == VK_SUCCESS;
}
