#include "FrameGraph.hpp"
#include "RasterizeNode.hpp"
#include "ComputeNode.hpp"

FrameGraph::FrameGraph()
{

}

FrameGraph::~FrameGraph()
{

}

void FrameGraph::Configure(const YAML::Node& graphConfiguration, const CommonFrameData& commonFrameData)
{
	for (int n = 0; n < graphConfiguration.size(); ++n)
	{
		// TODO: Get type into utils.
		std::string type = graphConfiguration[n]["type"].as<std::string>();
		std::string name = FrameGraphConfigurator::GetName(graphConfiguration[n]["name"]);
		// TODO: Handle errors.
		bool isFinal = false;
		if (graphConfiguration[n]["final"] && graphConfiguration[n]["final"].as<bool>() == true)
		{
			isFinal = true;
		}
		if (type == "rasterized")
		{
			nodes[name] = std::make_unique<RasterizeNode>(name, commonFrameData.framesInFlightCount, isFinal);
		}
		else if (type == "computed")
		{
			nodes[name] = std::make_unique<ComputeNode>(name, commonFrameData.framesInFlightCount, isFinal);
		}
		else
		{
			CoreLogFatal(VulkanLogger, "Configuration: Node can only be rasterized or computed.");
			return;
		}

		if (isFinal)
		{
			finalNode = nodes[name].get();
		}
	}

	RecursivelyConfigure(finalNode, nullptr, graphConfiguration, commonFrameData);
}

void FrameGraph::Shutdown(const CommonFrameData& commonFrameData)
{
	for (auto&& node : nodes)
	{
		node.second->Shutdown(commonFrameData);
	}
}

void FrameGraph::Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData)
{
	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
	
	VkViewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = (float)commonFrameData.surfaceData->width;
	viewport.height = (float)commonFrameData.surfaceData->height;
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = VkExtent2D{ (uint32_t)commonFrameData.surfaceData->width, (uint32_t)commonFrameData.surfaceData->height };
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	RecursivelyRecord(commandBuffer, frameInFlight, commonFrameData, finalNode, nullptr);
	vkEndCommandBuffer(commandBuffer);
}

void FrameGraph::Resize(const CommonFrameData& commonFrameData)
{
	RecursivelyResize(finalNode, commonFrameData);
}

void FrameGraph::UpdatePreallocatedUniformData(const std::string& nodeName, const std::string& name, int frameInFlight,
	void* data, int dataSize)
{
	// TODO: Handle error.
	nodes[nodeName]->UpdatePreallocatedUniformData(name, frameInFlight, data, dataSize);
}

void FrameGraph::UpdateTexture(const std::string& nodeName, const std::string& name, int frameInFlight,
	HawkEye::HTexture texture)
{
	// TODO: Handle error.
	nodes[nodeName]->UpdateTexture(name, frameInFlight, texture);
}

void FrameGraph::UpdateStorageBuffer(const std::string& nodeName, const std::string& name, int frameInFlight,
	HawkEye::HBuffer storageBuffer)
{
	// TODO: Handle error.
	nodes[nodeName]->UpdateStorageBuffer(name, frameInFlight, storageBuffer);
}

HawkEye::HMaterial FrameGraph::CreateMaterial(const std::string& nodeName, void* data, int dataSize)
{
	// TODO: Handle error.
	return nodes[nodeName]->CreateMaterial(data, dataSize);
}

void FrameGraph::UseBuffers(const std::string& nodeName, HawkEye::Pipeline::DrawBuffer* drawBuffers, int bufferCount)
{
	// TODO: Handle error.
	nodes[nodeName]->UseBuffers(drawBuffers, bufferCount);
}

void FrameGraph::RecursivelyConfigure(FrameGraphNode* node, FrameGraphNode* nextNode, const YAML::Node& graphConfiguration,
	const CommonFrameData& commonFrameData)
{
	// TODO: Set up a graph for easier recording.

	// Find this node in configuration.
	int i = -1;
	for (int n = 0; n < graphConfiguration.size(); ++n)
	{
		std::string name = FrameGraphConfigurator::GetName(graphConfiguration[n]["name"]);
		if (node->GetName() == name)
		{
			i = n;
			break;
		}
	}

	// Get all previous nodes.
	auto inputCharacteristics = FrameGraphConfigurator::GetInputCharacteristics(graphConfiguration[i]["input"]);

	// Assemble all previous nodes.
	std::set<std::string> dependencies;
	for (int c = 0; c < inputCharacteristics.size(); ++c)
	{
		if (inputCharacteristics[c].colorTarget && inputCharacteristics[c].colorTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].colorTarget->connectionName);
		}
		if (inputCharacteristics[c].depthTarget && inputCharacteristics[c].depthTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].depthTarget->connectionName);
		}
		if (inputCharacteristics[c].sampleTarget && inputCharacteristics[c].sampleTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].sampleTarget->connectionName);
		}
	}

	// Use input nodes' outputs to configure this node.
	std::vector<NodeOutputs*> nodeInputs;
	for (const auto& dependency : dependencies)
	{
		auto it = nodes.find(dependency);
		if (it == nodes.end())
		{
			CoreLogFatal(VulkanLogger, "Configuration: Incomplete graph.");
			return;
		}
		RecursivelyConfigure(nodes[dependency].get(), node, graphConfiguration, commonFrameData);
		nodeInputs.push_back(nodes[dependency]->GetOutputs());
	}

	// TODO: Use swapchain logic, render pass logic
	const bool first = dependencies.empty();
	const bool last = nextNode == nullptr;

	VkRenderPass renderPass = VK_NULL_HANDLE;

	if (first && last)
	{
		renderPass = commonFrameData.renderPassUP;
	}
	else if (first) // && !last
	{
		renderPass = commonFrameData.renderPassUA;
	}
	else if (last) // && !first
	{
		renderPass = commonFrameData.renderPassGP;
	}
	else // !first && !last
	{
		renderPass = commonFrameData.renderPassGA;
	}

	const bool nextInheritsSwapchain = nextNode && nextNode->GetType() == FrameGraphNodeType::Rasterized && nextNode->IsFinal();

	node->Configure(graphConfiguration[i], nodeInputs, inputCharacteristics, commonFrameData, renderPass, last || nextInheritsSwapchain);
}

struct OutputReference
{
	const OutputTargetCharacteristics& reference;
};

const OutputTargetCharacteristics& FrameGraph::RecursivelyRecord(VkCommandBuffer commandBuffer, int frameInFlight,
	const CommonFrameData& commonFrameData, FrameGraphNode* node, FrameGraphNode* nextNode)
{
	// Get all previous nodes.
	const auto& inputCharacteristics = node->GetInputCharacteristics();

	// Assemble all previous nodes.
	std::set<std::string> dependencies;
	for (int c = 0; c < inputCharacteristics.size(); ++c)
	{
		if (inputCharacteristics[c].colorTarget && inputCharacteristics[c].colorTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].colorTarget->connectionName);
		}
		if (inputCharacteristics[c].depthTarget && inputCharacteristics[c].depthTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].depthTarget->connectionName);
		}
		if (inputCharacteristics[c].sampleTarget && inputCharacteristics[c].sampleTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].sampleTarget->connectionName);
		}
	}

	// Use input nodes' outputs to configure this node.
	bool allDependenciesCompute = true;
	std::vector<OutputReference> outputCharacteristics;
	for (const auto& dependency : dependencies)
	{
		outputCharacteristics.push_back(
			{ RecursivelyRecord(commandBuffer, frameInFlight, commonFrameData, nodes[dependency].get(), node) });
		if (nodes[dependency]->GetType() == FrameGraphNodeType::Rasterized)
		{
			allDependenciesCompute = false;
		}
	}

	// TODO: Transition and render pass logic.
	// TODO: Handle empty records.
	const bool startPass = dependencies.empty() || allDependenciesCompute;
	const bool endPass = nextNode && nextNode->GetType() == FrameGraphNodeType::Computed;

	bool containsData = node->Record(commandBuffer, frameInFlight, commonFrameData, startPass, endPass);

	return node->GetOutputCharacteristics();
}

void FrameGraph::RecursivelyResize(FrameGraphNode* node, const CommonFrameData& commonFrameData)
{
	// Get all previous nodes.
	const auto& inputCharacteristics = node->GetInputCharacteristics();

	// Assemble all previous nodes.
	std::set<std::string> dependencies;
	for (int c = 0; c < inputCharacteristics.size(); ++c)
	{
		if (inputCharacteristics[c].colorTarget && inputCharacteristics[c].colorTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].colorTarget->connectionName);
		}
		if (inputCharacteristics[c].depthTarget && inputCharacteristics[c].depthTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].depthTarget->connectionName);
		}
		if (inputCharacteristics[c].sampleTarget && inputCharacteristics[c].sampleTarget->connectionName != "")
		{
			dependencies.insert(inputCharacteristics[c].sampleTarget->connectionName);
		}
	}

	// Use input nodes' outputs to configure this node.
	std::vector<NodeOutputs*> nodeInputs;
	for (const auto& dependency : dependencies)
	{
		auto it = nodes.find(dependency);
		if (it == nodes.end())
		{
			CoreLogFatal(VulkanLogger, "Resize: Incomplete graph.");
			return;
		}
		RecursivelyResize(nodes[dependency].get(), commonFrameData);
		nodeInputs.push_back(nodes[dependency]->GetOutputs());
	}

	// TODO: Use swapchain logic, render pass logic
	node->Resize(commonFrameData, nodeInputs);
}
