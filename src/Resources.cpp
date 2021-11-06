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
	VkSemaphore operationSemaphore = VK_NULL_HANDLE;
	VkSemaphore uploadSemaphore = VK_NULL_HANDLE;
	int queueOwnership = -1;
	int mipCount = 1;
	TextureQueue currentUsage;
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

HawkEye::HTexture HawkEye::UploadTexture(HRendererData rendererData, void* data, int dataSize, int width, int height,
	TextureFormat format, ColorCompression colorCompression, TextureCompression textureCompression,
	bool generateMips, TextureQueue usage)
{
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

	texture->transferCommandBuffer = VulkanBackend::AllocateCommandBuffer(backendData, backendData.transferCommandPool);
	texture->generalCommandBuffer = VulkanBackend::AllocateCommandBuffer(backendData, backendData.generalCommandPool);
	texture->operationSemaphore = VulkanBackend::CreateSemaphore(backendData);
	texture->uploadSemaphore = VulkanBackend::CreateSemaphore(backendData);

	// All in a single command buffer.
	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VulkanCheck(vkBeginCommandBuffer(texture->transferCommandBuffer, &commandBufferBeginInfo));
	
	// Transition layout to dst optimal.
	VulkanBackend::TransitionImageLayout(texture->transferCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		texture->image.image, mipCount, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);
	texture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	// Copy from staging buffer to GPU.
	VulkanBackend::CopyBufferToImage(backendData, texture->stagingBuffer.buffer, texture->image.image, texture->imageLayout,
		texture->transferCommandBuffer, width, height, VK_IMAGE_ASPECT_COLOR_BIT);
	
	// Transition layout shader read only optimal.
	if (generateMips)
	{
		VulkanBackend::TransitionImageLayout(texture->transferCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			texture->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, 0, backendData.transferFamilyIndex, backendData.generalFamilyIndex);
	}
	else
	{
		int nextQueueIndex = usage == TextureQueue::General ? backendData.generalFamilyIndex : backendData.computeFamilyIndex;
		VulkanBackend::TransitionImageLayout(texture->transferCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			texture->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, 0, backendData.transferFamilyIndex, nextQueueIndex);
		texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VulkanCheck(vkEndCommandBuffer(texture->transferCommandBuffer));

	// Queue submit.
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &texture->transferCommandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &texture->operationSemaphore;
	if (!generateMips)
	{
		VulkanCheck(vkQueueSubmit(backendData.transferQueues[0], 1, &submitInfo, texture->uploadFence));
	}

	if (generateMips)
	{
		VulkanCheck(vkQueueSubmit(backendData.transferQueues[0], 1, &submitInfo, nullptr));

		VulkanCheck(vkBeginCommandBuffer(texture->generalCommandBuffer, &commandBufferBeginInfo));

		VulkanBackend::AcquireImageOwnership(backendData, texture->generalCommandBuffer, texture->image.image, mipCount,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, texture->imageLayout,
			VK_ACCESS_TRANSFER_READ_BIT, backendData.transferFamilyIndex, backendData.computeFamilyIndex);

		VulkanBackend::GenerateMips(backendData, texture->generalCommandBuffer, texture->image.image, imageFormat, width, height, mipCount);
		texture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		int pastQueueIndex = usage == TextureQueue::General ? VK_QUEUE_FAMILY_IGNORED : backendData.generalFamilyIndex;
		int nextQueueIndex = usage == TextureQueue::General ? VK_QUEUE_FAMILY_IGNORED : backendData.computeFamilyIndex;
		VulkanBackend::TransitionImageLayout(texture->generalCommandBuffer, texture->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			texture->image.image, mipCount, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, 0, pastQueueIndex, nextQueueIndex);
		texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VulkanCheck(vkEndCommandBuffer(texture->generalCommandBuffer));

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &texture->generalCommandBuffer;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &texture->operationSemaphore;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &texture->uploadSemaphore;
		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		submitInfo.pWaitDstStageMask = &waitStageMask;

		VulkanCheck(vkQueueSubmit(backendData.generalQueues[0], 1, &submitInfo, texture->uploadFence));
		if (usage == TextureQueue::General)
		{
			texture->queueOwnership = backendData.generalFamilyIndex;
		}
	}

	return texture;
}

void HawkEye::DeleteTexture(HRendererData rendererData, HTexture& texture)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	WaitForUpload(rendererData, texture);

	VulkanBackend::FreeCommandBuffer(backendData, backendData.transferCommandPool, texture->transferCommandBuffer);
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

struct HawkEye::HBuffer_t
{
	VulkanBackend::Buffer buffer{};
	VkFence uploadFence = VK_NULL_HANDLE;
	VkCommandBuffer transferCommandBuffer = VK_NULL_HANDLE;
	VulkanBackend::Buffer stagingBuffer{};
	void* mappedBuffer = nullptr;
	int dataSize = 0;
};

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
	{
		void* stagingBufferData;
		VulkanCheck(vmaMapMemory(backendData.allocator, buffer->stagingBuffer.allocation, &stagingBufferData));
		memcpy(stagingBufferData, data, dataSize);
		vmaUnmapMemory(backendData.allocator, buffer->stagingBuffer.allocation);
	}

	buffer->uploadFence = VulkanBackend::CreateFence(backendData);

	buffer->transferCommandBuffer = VulkanBackend::AllocateCommandBuffer(backendData, backendData.transferCommandPool);

	// All in a single command buffer.
	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VulkanCheck(vkBeginCommandBuffer(buffer->transferCommandBuffer, &commandBufferBeginInfo));

	VulkanBackend::CopyBufferToBuffer(backendData, buffer->stagingBuffer.buffer, buffer->buffer.buffer, dataSize,
		buffer->transferCommandBuffer);

	// TODO: Barrier to transfer ownership.

	VulkanCheck(vkEndCommandBuffer(buffer->transferCommandBuffer));

	// Queue submit.
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &buffer->transferCommandBuffer;

	VulkanCheck(vkQueueSubmit(backendData.transferQueues[0], 1, &submitInfo, buffer->uploadFence));

	return buffer;
}

void HawkEye::DeleteBuffer(HRendererData rendererData, HBuffer& buffer)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	WaitForUpload(rendererData, buffer);

	if (buffer->uploadFence != VK_NULL_HANDLE)
	{
		VulkanBackend::FreeCommandBuffer(backendData, backendData.transferCommandPool, buffer->transferCommandBuffer);
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
		CoreLogError(VulkanLogger, "Data upload: Specified data size is bigger than buffer size.");
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
		submitInfo.pCommandBuffers = &buffer->transferCommandBuffer;

		VulkanCheck(vkQueueSubmit(backendData.transferQueues[0], 1, &submitInfo, buffer->uploadFence));
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
