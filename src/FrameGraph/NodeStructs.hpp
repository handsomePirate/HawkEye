#pragma once
#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

enum class FrameGraphNodeType
{
	Rasterized,
	Computed
};

enum class TargetType
{
	Color,
	Depth
};

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
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;

	VkSampler targetSampler;

	VkCommandPool commandPool;
	std::vector<CommandBufferData> commandBuffers;

	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue computeQueue = VK_NULL_HANDLE;

	uint64_t currentFrame = 0;
};

struct Target
{
	VulkanBackend::Image image;
	VkImageView imageView;
	bool inherited;
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

struct ImageFormat
{
	VkFormat format = VK_FORMAT_UNDEFINED;
	enum class Metadata {
		Specified,
		ColorOptimal,
		DepthOptimal
	} metadata = Metadata::Specified;
	bool Equals(const ImageFormat& other) const
	{
		return format == other.format && metadata == other.metadata;
	}
	VkFormat Resolve(const VulkanBackend::SurfaceData& surfaceData) const
	{
		if (metadata == Metadata::Specified)
		{
			return format;
		}
		if (metadata == Metadata::ColorOptimal)
		{
			return surfaceData.surfaceFormat.format;
		}
		return surfaceData.depthFormat;
	}
};

struct InputImageCharacteristics
{
	float widthModifier;
	float heightModifier;
	ImageFormat imageFormat;
	std::string connectionName;
	int connectionSlot;
	ContentOperation contentOperation;
};

struct OutputImageCharacteristics
{
	float widthModifier;
	float heightModifier;
	ImageFormat imageFormat;
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