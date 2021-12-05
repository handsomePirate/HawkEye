#pragma once
#include "HawkEye/HawkEyeAPI.hpp"
#include "YAMLConfiguration.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>

struct FrameData
{
	bool dirty = true;
	VkCommandBuffer commandBuffer;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
};

struct HawkEye::Pipeline::Private
{
	// TODO: Currently only one layer supported.
	std::vector<PipelinePass> passes;
	VulkanBackend::BackendData* backendData = nullptr;
	std::unique_ptr<VulkanBackend::SurfaceData> surfaceData = nullptr;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> framebuffers;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	std::vector<VkShaderModule> shaderModules;
	VkPipeline computePipeline = VK_NULL_HANDLE;
	VkPipeline rasterizationPipeline = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue computeQueue = VK_NULL_HANDLE;
	VkSemaphore graphicsSemaphore = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	std::vector<VkFence> frameFences;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	std::map<std::string, HBuffer> uniformBuffers;
	std::map<std::string, int> uniformTextureBindings;
	std::map<std::string, HTexture> uniformTextures;
	std::vector<FrameData> frameData;
	// TODO: Layers.
	int vertexSize = 1;
	uint64_t currentFrame = 0;
	HawkEye::Pipeline::DrawBuffer* drawBuffers = nullptr;
	int drawBufferCount = 0;
};

void RecordCommands(int c, const VulkanBackend::BackendData& backendData, HawkEye::Pipeline::Private* pipelineData);
