#pragma once
#include "HawkEye/HawkEyeAPI.hpp"
#include "../Framebuffer.hpp"
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

struct CommandBufferData
{
	bool dirty = true;
	bool toBeReleased = false;
	VkCommandBuffer commandBuffer;
};

struct CommonFrameData
{
	HawkEye::HRendererData rendererData = nullptr;
	VulkanBackend::BackendData* backendData = nullptr;
	std::unique_ptr<VulkanBackend::SurfaceData> surfaceData = nullptr;
	int framesInFlightCount;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass renderPassUP = VK_NULL_HANDLE;
	VkRenderPass renderPassUA = VK_NULL_HANDLE;
	VkRenderPass renderPassGA = VK_NULL_HANDLE;
	VkRenderPass renderPassGP = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;

	VkSampler targetSampler;

	VkCommandPool commandPool;
	std::vector<CommandBufferData> commandBuffers;

	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue computeQueue = VK_NULL_HANDLE;

	uint64_t currentFrame = 0;
};

struct NodeOutputs
{
	std::unique_ptr<Target> colorTarget = nullptr;
	std::unique_ptr<Target> depthTarget = nullptr;
	std::unique_ptr<Target> sampleTarget = nullptr;
	// TODO: In storage buffers.
};

enum class ContentOperation
{
	Clear,
	DontCare,
	Preserve
};

struct InputImageCharacteristics
{
	float widthModifier;
	float heightModifier;
	VkFormat format = VK_FORMAT_UNDEFINED;
	std::string connectionName;
	int connectionSlot;
	ContentOperation contentOperation;
};

struct OutputImageCharacteristics
{
	float widthModifier;
	float heightModifier;
	VkFormat format = VK_FORMAT_UNDEFINED;
	bool read;
	bool write;
};

struct InputTargetCharacteristics
{
	std::unique_ptr<InputImageCharacteristics> colorTarget = nullptr;
	std::unique_ptr<InputImageCharacteristics> depthTarget = nullptr;
	std::unique_ptr<InputImageCharacteristics> sampleTarget = nullptr;
	// TODO: In storage buffers.
};

struct OutputTargetCharacteristics
{
	std::unique_ptr<OutputImageCharacteristics> colorTarget = nullptr;
	std::unique_ptr<OutputImageCharacteristics> depthTarget = nullptr;
	std::unique_ptr<OutputImageCharacteristics> sampleTarget = nullptr;
	// TODO: In storage buffers.
};