#pragma once
#include "FrameGraphNode.hpp"

class RasterizeNode : public FrameGraphNode
{
public:
	virtual ~RasterizeNode();
	// TODO: Provide swapchain as output.
	void Configure(const YAML::Node& nodeConfiguration, int framesInFlightCount,
		const std::vector<NodeOutputs*>& nodeInputs, std::vector<InputTargetCharacteristics>& inputCharacteristics,
		const CommonFrameData& commonFrameData, VkRenderPass renderPassReference, bool useSwapchain) override;
	// TODO: Change for layer and common information.
	bool Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData,
		bool startRenderPass, bool endRenderPass) override;
	void Resize(const CommonFrameData& commonFrameData) override;

private:
	int vertexSize = 0;
};
