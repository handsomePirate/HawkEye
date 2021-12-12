#pragma once
#include "HawkEye/HawkEyeAPI.hpp"
#include "YAMLConfiguration.hpp"
#include "Framebuffer.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <map>

struct DescriptorData
{
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct FrameData
{
	bool dirty = true;
	VkCommandBuffer commandBuffer;
};

struct PipelinePassData
{
	int dimension;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	std::vector<VkShaderModule> shaderModules;
	VkPipeline computePipeline = VK_NULL_HANDLE;
	VkPipeline rasterizationPipeline = VK_NULL_HANDLE;
	int materialSize = 0;
	std::vector<DescriptorData> materials;
	std::vector<UniformData> materialData;
	std::map<std::string, HawkEye::HBuffer> materialBuffers;
	int vertexSize = 1;
	std::map<int, std::vector<HawkEye::Pipeline::DrawBuffer>> drawBuffers;
	// TODO: One should be enough.
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	std::map<std::string, HawkEye::HBuffer> uniformBuffers;
	std::map<std::string, int> uniformTextureBindings;
	std::map<std::string, HawkEye::HTexture> uniformTextures;
	DescriptorData descriptorData;
};

struct HawkEye::Pipeline::Private
{
	int samples = 0;
	bool hasDepthTarget = false;
	PipelineUniforms uniformInfo;
	std::vector<PipelineTarget> pipelineTargets;
	std::vector<Target> targets;
	std::vector<PipelinePass> passes;
	std::vector<PipelinePassData> passData;
	VulkanBackend::BackendData* backendData = nullptr;
	std::unique_ptr<VulkanBackend::SurfaceData> surfaceData = nullptr;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> framebuffers;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue computeQueue = VK_NULL_HANDLE;
	VkSemaphore graphicsSemaphore = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	std::vector<VkFence> frameFences;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::map<std::string, HBuffer> uniformBuffers;
	std::map<std::string, int> uniformTextureBindings;
	std::map<std::string, HTexture> uniformTextures;
	DescriptorData descriptorData;
	std::vector<FrameData> frameData;
	uint64_t currentFrame = 0;
};

namespace PipelineUtils
{
	VkFormat GetAttributeFormat(const PipelinePass::VertexAttribute& vertexAttribute);
}
