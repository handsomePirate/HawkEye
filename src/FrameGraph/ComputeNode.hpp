#pragma once
#include "FrameGraphNode.hpp"

class ComputeNode : public FrameGraphNode
{
public:
	ComputeNode(const std::string& name, int framesInFlightCount, bool isFinal);
	virtual ~ComputeNode();
	// TODO: Provide swapchain as output.
	void Configure(const YAML::Node& nodeConfiguration,
		const std::vector<NodeOutputs*>& nodeInputs, std::vector<InputTargetCharacteristics>& inputCharacteristics,
		OutputTargetCharacteristics& outputCharacteristics,
		const CommonFrameData& commonFrameData, VkRenderPass renderPassReference, bool useSwapchain) override;
	void Shutdown(const CommonFrameData& commonFrameData) override;
	// TODO: Change for layer and common information.
	bool Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData,
		bool startRenderPass, bool endRenderPass) override;
	void Resize(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs) override;

	void CreateColorTarget(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs);
	void CreateDepthTarget(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs);
	void CreateSampleTarget(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs);

private:
	DescriptorSystem targetDescriptorSystem;
	VkDescriptorSetLayout targetDescriptorSystemLayout;
};

