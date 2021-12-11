#pragma once
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <unordered_map>

struct HawkEye::HTexture_t
{
	VulkanBackend::Image image;
	VkImageView imageView = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkFence uploadFence = VK_NULL_HANDLE;
	VkCommandBuffer generalCommandBuffer = VK_NULL_HANDLE;
	VulkanBackend::Buffer stagingBuffer{};
	VkSemaphore operationSemaphore = VK_NULL_HANDLE;
	VkSemaphore uploadSemaphore = VK_NULL_HANDLE;
	int mipCount = 1;
	bool firstUse = true;
	int currentFamilyIndex;
	TextureQueue currentUsage;
};

struct HawkEye::HBuffer_t
{
	VulkanBackend::Buffer buffer{};
	VkFence uploadFence = VK_NULL_HANDLE;
	VkCommandBuffer generalCommandBuffer = VK_NULL_HANDLE;
	VulkanBackend::Buffer stagingBuffer{};
	void* mappedBuffer = nullptr;
	int dataSize = 0;
	bool firstUse = true;
	int currentFamilyIndex;
};
