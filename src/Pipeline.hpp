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
	bool toBeReleased = false;
	VkCommandBuffer commandBuffer;
};

enum class PhaseType
{
	UP,
	UA,
	GA,
	GP
};

struct PhaseData
{
	PhaseType type;
	VkRenderPass renderPass;
	std::vector<VkFramebuffer> framebuffers;
	Target colorTarget;
};

struct PipelinePassData
{
	int dimension;
	bool inheritDepth = false;
	bool empty = true;
	int colorTarget;
	PipelinePass::Type type = PipelinePass::Type::Undefined;
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
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	std::vector<VkDescriptorSetLayout> passUniformLayouts;
	std::map<std::string, int> storageBufferBindings;
	std::map<std::string, HawkEye::HBuffer> uniformBuffers;
	std::map<std::string, int> uniformTextureBindings;
	std::map<std::string, HawkEye::HTexture> uniformTextures;
	DescriptorData descriptorData;
	std::vector<HawkEye::HBuffer> frameDecriptorBuffers;
	std::vector<DescriptorData> frameDescriptors;
	VkDescriptorSetLayout frameDescriptorLayout = VK_NULL_HANDLE;
};

struct HawkEye::Pipeline::Private
{
	int samples = 0;
	bool configured = false;
	bool hasDepthTarget = false;
	bool containsComputedPass = false;
	PipelineUniforms uniformInfo;
	std::vector<PhaseData> phases;
	VkSampler targetSampler = VK_NULL_HANDLE;
	std::vector<PipelineTarget> pipelineTargets;
	std::vector<Target> targets;
	std::vector<PipelinePass> passes;
	std::vector<PipelinePassData> passData;
	VulkanBackend::BackendData* backendData = nullptr;
	std::unique_ptr<VulkanBackend::SurfaceData> surfaceData = nullptr;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass renderPassUP = VK_NULL_HANDLE;
	VkRenderPass renderPassUA = VK_NULL_HANDLE;
	VkRenderPass renderPassGA = VK_NULL_HANDLE;
	VkRenderPass renderPassGP = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue computeQueue = VK_NULL_HANDLE;
	VkSemaphore graphicsSemaphore = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	std::vector<VkFence> frameFences;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::map<std::string, int> storageBufferBindings;
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
