#include "FrameGraph.hpp"
#include "RasterizeNode.hpp"
#include "ComputeNode.hpp"
#include <SoftwareCore/DefaultLogger.hpp>
#include <VulkanBackend/ErrorCheck.hpp>

FrameGraph::FrameGraph()
{

}

FrameGraph::~FrameGraph()
{

}

void FrameGraph::Configure(const YAML::Node& graphConfiguration, const CommonFrameData& commonFrameData)
{
	backendData = commonFrameData.backendData;

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
			CoreLogFatal(DefaultLogger, "Configuration: Node can only be rasterized or computed.");
			return;
		}

		if (isFinal)
		{
			finalNode = nodes[name].get();
		}
	}

	RecursivelyConfigure(finalNode, nullptr, graphConfiguration, commonFrameData, nullptr);

	PruneGraph();
}

void FrameGraph::Shutdown(const CommonFrameData& commonFrameData)
{
	for (auto&& renderPass : renderPasses)
	{
		VulkanBackend::DestroyRenderPass(*backendData, renderPass);
	}

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
	auto node = nodes.find(nodeName);
	if (node == nodes.end())
	{
		CoreLogError(DefaultLogger, "Uniform update: No node \'%s\' is configured in the frame graph (could have been pruned).",
			nodeName.c_str());
		return;
	}
	nodes[nodeName]->UpdatePreallocatedUniformData(name, frameInFlight, data, dataSize);
}

void FrameGraph::UpdateTexture(const std::string& nodeName, const std::string& name, int frameInFlight,
	HawkEye::HTexture texture)
{
	auto node = nodes.find(nodeName);
	if (node == nodes.end())
	{
		CoreLogError(DefaultLogger, "Texture update: No node \'%s\' is configured in the framegraph (could have been pruned).",
			nodeName.c_str());
		return;
	}
	nodes[nodeName]->UpdateTexture(name, frameInFlight, texture);
}

void FrameGraph::UpdateStorageBuffer(const std::string& nodeName, const std::string& name, int frameInFlight,
	HawkEye::HBuffer storageBuffer)
{
	auto node = nodes.find(nodeName);
	if (node == nodes.end())
	{
		CoreLogError(DefaultLogger, "Buffer update: No node \'%s\' is configured in the frame graph (could have been pruned).",
			nodeName.c_str());
		return;
	}
	nodes[nodeName]->UpdateStorageBuffer(name, frameInFlight, storageBuffer);
}

HawkEye::HMaterial FrameGraph::CreateMaterial(const std::string& nodeName, void* data, int dataSize)
{
	auto node = nodes.find(nodeName);
	if (node == nodes.end())
	{
		CoreLogError(DefaultLogger, "Material creation: No node \'%s\' is configured in the frame graph (could have been pruned).",
			nodeName.c_str());
		return -1;
	}
	return nodes[nodeName]->CreateMaterial(data, dataSize);
}

void FrameGraph::UseBuffers(const std::string& nodeName, HawkEye::Pipeline::DrawBuffer* drawBuffers, int bufferCount)
{
	auto node = nodes.find(nodeName);
	if (node == nodes.end())
	{
		CoreLogError(DefaultLogger, "Buffer usage: No node \'%s\' is configured in the frame graph (could have been pruned).",
			nodeName.c_str());
		return;
	}
	nodes[nodeName]->UseBuffers(drawBuffers, bufferCount);
}

VkAttachmentDescription GetAttachmentDescription(const CommonFrameData& commonFrameData,
	const InputImageCharacteristics* const inputCharacteristics,
	const OutputImageCharacteristics* const outputCharacteristics,
	bool first, bool last, bool depthStencil)
{
	VkAttachmentDescription result{};

	VkFormat colorFormat = outputCharacteristics->imageFormat.Resolve(*commonFrameData.surfaceData);

	result.format = colorFormat;
	// TODO: Sample count.
	result.samples = VK_SAMPLE_COUNT_1_BIT;

	VkImageLayout initialLayout;
	VkImageLayout finalLayout;

	if (first && last)
	{
		initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (depthStencil)
		{
			finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		else
		{
			finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
	}
	else if (first) // && !last
	{
		initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	else if (last) // && !first
	{
		initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (depthStencil)
		{
			finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		else
		{
			finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
	}
	else // !first && !last
	{
		initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	if (!inputCharacteristics)
	{
		initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	// TODO: Multiple targets.
	if (!inputCharacteristics || inputCharacteristics->contentOperation == ContentOperation::Clear)
	{
		result.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	}
	else if (inputCharacteristics->contentOperation == ContentOperation::Preserve)
	{
		result.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	else if (inputCharacteristics->contentOperation == ContentOperation::DontCare)
	{
		result.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}

	result.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	result.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	result.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	result.initialLayout = initialLayout;
	result.finalLayout = finalLayout;

	return result;
}

VkRenderPass FrameGraph::RecursivelyConfigure(FrameGraphNode* node, FrameGraphNode* nextNode, const YAML::Node& graphConfiguration,
	const CommonFrameData& commonFrameData, const std::vector<InputTargetCharacteristics>* nextInputCharacteristics)
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
	VkRenderPass renderPass = VK_NULL_HANDLE;
	for (const auto& dependency : dependencies)
	{
		auto it = nodes.find(dependency);
		if (it == nodes.end())
		{
			CoreLogFatal(DefaultLogger, "Configuration: Incomplete graph.");
			return VK_NULL_HANDLE;
		}
		// TODO: Handle two forks coming together (render pass wise).
		renderPass = RecursivelyConfigure(nodes[dependency].get(), node, graphConfiguration,
			commonFrameData, &inputCharacteristics);
		nodeInputs.push_back(nodes[dependency]->GetOutputs());
	}

	auto outputCharacteristics = FrameGraphConfigurator::GetOutputCharacteristics(graphConfiguration[i]["output"]);

	// TODO: First and last for each attachment separately.
	const bool first = dependencies.empty();
	const bool last = nextNode == nullptr || nextNode->IsFinalBlock();

	if (node->GetType() == FrameGraphNodeType::Rasterized)
	{
		// TODO: Handle different render pass attachments to the previous node.
		node->SetIsFinalBlock(last);
		if (renderPass == VK_NULL_HANDLE)
		{
			
			// TODO: Create render pass and store it to be destroyed (reference will be passed into respective nodes).
			const int attachmentCount = 
				(outputCharacteristics.colorTarget ? 1 : 0) + 
				(outputCharacteristics.depthTarget ? 1 : 0) + 
				(outputCharacteristics.sampleTarget ? 1 : 0);
			std::vector<VkAttachmentDescription> attachments(attachmentCount);

			int attachmentIndex = 0;
			std::vector<VkAttachmentReference> colorAttachments;

			// Color attachment.
			if (outputCharacteristics.colorTarget)
			{
				attachments[attachmentIndex] = GetAttachmentDescription(commonFrameData,
					inputCharacteristics[0].colorTarget.get(),
					outputCharacteristics.colorTarget.get(),
					first, last, false);
				VkAttachmentReference reference{};
				reference.attachment = uint32_t(attachmentIndex);
				reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorAttachments.push_back(reference);
				++attachmentIndex;
			}

			// Depth stencil attachment.
			VkAttachmentReference depthReference{};
			if (outputCharacteristics.depthTarget)
			{
				const bool depthFirst = inputCharacteristics[0].depthTarget && !outputCharacteristics.depthTarget->read;
				// TODO: Check all such connections pointing to this one, not just zeroth input characteristic.
				const bool depthLast = nextNode == nullptr || nextNode->IsFinalBlock() ||
					(!(*nextInputCharacteristics)[0].depthTarget || (*nextInputCharacteristics)[0].depthTarget->connectionName != node->GetName());
				attachments[attachmentIndex] = GetAttachmentDescription(commonFrameData,
					inputCharacteristics[0].depthTarget.get(),
					outputCharacteristics.depthTarget.get(),
					depthFirst, depthLast, true);
				depthReference.attachment = uint32_t(attachmentIndex);
				depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				++attachmentIndex;
			}

			// Sample attachment.
			if (outputCharacteristics.sampleTarget)
			{
				attachments[attachmentIndex] = GetAttachmentDescription(commonFrameData,
					inputCharacteristics[0].sampleTarget.get(),
					outputCharacteristics.sampleTarget.get(),
					first, last, false);
				VkAttachmentReference reference{};
				reference.attachment = uint32_t(attachmentIndex);
				reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorAttachments.push_back(reference);
				++attachmentIndex;
			}

			VkSubpassDescription subpassDescription{};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = uint32_t(colorAttachments.size());
			subpassDescription.pColorAttachments = colorAttachments.data();
			if (outputCharacteristics.depthTarget)
			{
				subpassDescription.pDepthStencilAttachment = &depthReference;
			}

			// Subpass dependencies for layout transitions
			const int dependencyCount = 2;
			VkSubpassDependency dependencies[dependencyCount];

			dependencies[0] = {};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1] = {};
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = attachmentCount;
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpassDescription;
			renderPassInfo.dependencyCount = dependencyCount;
			renderPassInfo.pDependencies = dependencies;

			VulkanCheck(vkCreateRenderPass(backendData->logicalDevice, &renderPassInfo, nullptr, &renderPass));

			renderPasses.push_back(renderPass);
		}
	}
	else
	{
		renderPass = VK_NULL_HANDLE;
	}

	const bool nextInheritsSwapchain = nextNode && nextNode->IsFinalBlock();

	node->Configure(graphConfiguration[i], nodeInputs, inputCharacteristics, outputCharacteristics,
		commonFrameData, renderPass, last || nextInheritsSwapchain);

	return renderPass;
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
	const bool endPass = !nextNode || nextNode->GetType() == FrameGraphNodeType::Computed;

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
			CoreLogFatal(DefaultLogger, "Resize: Incomplete graph.");
			return;
		}
		RecursivelyResize(nodes[dependency].get(), commonFrameData);
		nodeInputs.push_back(nodes[dependency]->GetOutputs());
	}

	// TODO: Use swapchain logic, render pass logic
	node->Resize(commonFrameData, nodeInputs);
}

void FrameGraph::PruneGraph()
{
	std::map<std::string, std::unique_ptr<FrameGraphNode>> newNodes;
	for (auto&& node : nodes)
	{
		if (node.second->IsConfigured())
		{
			newNodes[node.first] = std::move(node.second);
		}
	}
	std::swap(nodes, newNodes);
}
