#pragma once
#include "NodeStructs.hpp"
#include "../DescriptorSystem.hpp"
#include <yaml-cpp/yaml.h>
#include <vulkan/vulkan.hpp>

class FrameGraphNode
{
public:
	FrameGraphNode(const std::string& name, int framesInFlightCount, FrameGraphNodeType type, bool isFinal);
	virtual ~FrameGraphNode();

	const std::string& GetName() const;
	FrameGraphNodeType GetType() const;
	bool IsFinal() const;

	virtual void Configure(const YAML::Node& nodeConfiguration,
		const std::vector<NodeOutputs*>& nodeInputs, std::vector<InputTargetCharacteristics>& inputCharacteristics,
		const CommonFrameData& commonFrameData, VkRenderPass renderPassReference, bool useSwapchain) = 0;

	virtual void Shutdown(const CommonFrameData& commonFrameData) = 0;

	virtual bool Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData,
		bool startRenderPass, bool endRenderPass) = 0;

	virtual void Resize(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs) = 0;

	void UpdatePreallocatedUniformData(const std::string& name, int frameInFlight, void* data, int dataSize);
	void UpdateTexture(const std::string& name, int frameInFlight, HawkEye::HTexture texture);
	void UpdateStorageBuffer(const std::string& name, int frameInFlight, HawkEye::HBuffer storageBuffer);

	NodeOutputs* GetOutputs();

	const std::vector<InputTargetCharacteristics>& GetInputCharacteristics() const;
	const OutputTargetCharacteristics& GetOutputCharacteristics();

	HawkEye::HMaterial CreateMaterial(void* data, int dataSize);
	// TODO: Update material?

	void UseBuffers(HawkEye::Pipeline::DrawBuffer* drawBuffers, int bufferCount);

protected:
	std::string name;
	FrameGraphNodeType type;
	int samples;
	int framesInFlightCount;
	bool useSwapchain;
	bool isFinal;
	bool reuseColorTarget;
	bool reuseDepthTarget;
	bool reuseSampleTarget;

	VulkanBackend::BackendData* backendData;
	HawkEye::HRendererData rendererData;

	std::vector<std::vector<HawkEye::Pipeline::DrawBuffer>> drawBuffers;

	VkRenderPass renderPassReference = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> framebuffers;
	VkDescriptorSetLayout uniformDescriptorSetLayout;
	VkDescriptorSetLayout materialDescriptorSetLayout;
	DescriptorSystem uniformDescriptorSystem;
	std::vector<UniformData> materialData;
	std::vector<std::unique_ptr<DescriptorSystem>> materialDescriptorSystems;
	std::vector<InputTargetCharacteristics> nodeInputCharacteristics;
	NodeOutputs nodeOutputs;
	OutputTargetCharacteristics nodeOutputCharacteristics;
	std::vector<VkShaderModule> shaderModules;
};
