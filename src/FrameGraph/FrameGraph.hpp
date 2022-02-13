#pragma once
#include "HawkEye/HawkEyeAPI.hpp"
#include "FrameGraphNode.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <yaml-cpp/yaml.h>

class FrameGraph
{
public:
	FrameGraph();
	~FrameGraph();

	void Configure(const YAML::Node& graphConfiguration, const CommonFrameData& commonFrameData);
	void Shutdown(const CommonFrameData& commonFrameData);

	void Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData);

	void Resize(const CommonFrameData& commonFrameData);
	
	void UpdatePreallocatedUniformData(const std::string& nodeName, const std::string& name, int frameInFlight, void* data, int dataSize);
	void UpdateTexture(const std::string& nodeName, const std::string& name, int frameInFlight, HawkEye::HTexture texture);
	void UpdateStorageBuffer(const std::string& nodeName, const std::string& name, int frameInFlight, HawkEye::HBuffer storageBuffer);

	HawkEye::HMaterial CreateMaterial(const std::string& nodeName, void* data, int dataSize);

	void UseBuffers(const std::string& nodeName, HawkEye::Pipeline::DrawBuffer* drawBuffers, int bufferCount);

private:
	void RecursivelyConfigure(FrameGraphNode* node, FrameGraphNode* nextNode, const YAML::Node& graphConfiguration,
		const CommonFrameData& commonFrameData);
	const OutputTargetCharacteristics& RecursivelyRecord(VkCommandBuffer commandBuffer, int frameInFlight,
		const CommonFrameData& commonFrameData, FrameGraphNode* node, FrameGraphNode* nextNode);
	void RecursivelyResize(FrameGraphNode* node, const CommonFrameData& commonFrameData);

	std::map<std::string, std::unique_ptr<FrameGraphNode>> nodes;
	FrameGraphNode* finalNode;
};
