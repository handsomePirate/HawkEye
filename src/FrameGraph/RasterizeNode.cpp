#include "RasterizeNode.hpp"
#include "../Resources.hpp"
#include "../Descriptors.hpp"
#include "../Pipeline.hpp"
#include <VulkanBackend/ErrorCheck.hpp>
#include <VulkanShaderCompiler/VulkanShaderCompilerAPI.hpp>

RasterizeNode::~RasterizeNode()
{

}

void RasterizeNode::Configure(const YAML::Node& nodeConfiguration, int framesInFlightCount,
	const std::vector<NodeOutputs*>& nodeInputs, std::vector<InputTargetCharacteristics>& inputCharacteristics,
	const CommonFrameData& commonFrameData, VkRenderPass renderPassReference, bool useSwapchain)
{
	backendData = commonFrameData.backendData;
	rendererData = commonFrameData.rendererData;
	FrameGraphNode::framesInFlightCount = framesInFlightCount;

	// inputs & outputs
	nodeInputCharacteristics = std::move(inputCharacteristics);
	nodeOutputCharacteristics = FrameGraphConfigurator::GetOutputCharacteristics(nodeConfiguration["output"]);
	// Create new targets, iff no slot is connected or the node is read-write
	// targets

	// color
	bool reuseColorTarget = false;
	bool preserveColor = false;
	if (nodeInputCharacteristics.size() == 1 && nodeInputCharacteristics[0].colorTarget &&
		nodeOutputCharacteristics.colorTarget && nodeOutputCharacteristics.colorTarget->write &&
		!nodeOutputCharacteristics.colorTarget->read &&
		nodeInputCharacteristics[0].colorTarget->format == nodeOutputCharacteristics.colorTarget->format &&
		nodeInputs.size() > 0)
		// TODO: nodeInputs also need to contain color target.
	{
		reuseColorTarget = true;
		if (nodeInputCharacteristics[0].colorTarget->contentOperation == ContentOperation::Preserve)
		{
			preserveColor = true;
		}
	}

	// depth
	bool reuseDepthTarget = false;
	bool preserveDepth = false;
	if (nodeInputCharacteristics.size() == 1 && nodeInputCharacteristics[0].depthTarget &&
		nodeOutputCharacteristics.depthTarget && nodeOutputCharacteristics.depthTarget->write &&
		!nodeOutputCharacteristics.depthTarget->read &&
		nodeInputCharacteristics[0].depthTarget->format == nodeOutputCharacteristics.depthTarget->format &&
		nodeInputs.size() > 0)
		// TODO: nodeInputs also need to contain depth target.
	{
		reuseDepthTarget = true;
		if (nodeInputCharacteristics[0].depthTarget->contentOperation == ContentOperation::Preserve)
		{
			preserveDepth = true;
		}
	}

	// sample
	bool reuseSampleTarget = false;
	bool preserveSample = false;
	if (nodeInputCharacteristics.size() == 1 && nodeInputCharacteristics[0].sampleTarget &&
		nodeOutputCharacteristics.sampleTarget && nodeOutputCharacteristics.sampleTarget->write &&
		!nodeOutputCharacteristics.sampleTarget->read &&
		nodeInputCharacteristics[0].sampleTarget->format == nodeOutputCharacteristics.sampleTarget->format &&
		nodeInputs.size() > 0)
		// TODO: nodeInputs also need to contain sample target.
	{
		reuseSampleTarget = true;
		if (nodeInputCharacteristics[0].sampleTarget->contentOperation == ContentOperation::Preserve)
		{
			preserveSample = true;
		}
	}

	if (!useSwapchain && nodeOutputCharacteristics.colorTarget)
	{
		if (nodeOutputCharacteristics.colorTarget && !reuseColorTarget)
		{
			nodeOutputs.colorTarget = std::make_unique<Target>(FramebufferUtils::CreateColorTarget(*backendData,
				*commonFrameData.surfaceData.get(), nodeOutputCharacteristics.colorTarget->format, !useSwapchain));
		}
		else
		{
			nodeOutputs.colorTarget = std::make_unique<Target>(
				Target{ nodeInputs[0]->colorTarget->image, nodeInputs[0]->colorTarget->imageView, nodeInputs[0]->colorTarget->inherited });
			nodeOutputs.colorTarget->inherited = true;
		}
	}

	if (nodeOutputCharacteristics.depthTarget)
	{
		if (!reuseDepthTarget)
		{
			nodeOutputs.depthTarget = std::make_unique<Target>(FramebufferUtils::CreateDepthTarget(*backendData,
				*commonFrameData.surfaceData.get(), nodeOutputCharacteristics.depthTarget->format));
		}
		else
		{
			nodeOutputs.depthTarget = std::make_unique<Target>(
				Target{ nodeInputs[0]->depthTarget->image, nodeInputs[0]->depthTarget->imageView, nodeInputs[0]->depthTarget->inherited });
			nodeOutputs.depthTarget->inherited = true;
		}
	}

	// TODO: Sample target in render passes.
	if (nodeOutputCharacteristics.sampleTarget)
	{
		if (!reuseSampleTarget)
		{
			nodeOutputs.sampleTarget = std::make_unique<Target>(FramebufferUtils::CreateColorTarget(*backendData,
				*commonFrameData.surfaceData.get(), nodeOutputCharacteristics.sampleTarget->format));
		}
		else
		{
			nodeOutputs.sampleTarget = std::make_unique<Target>(
				Target{ nodeInputs[0]->sampleTarget->image, nodeInputs[0]->sampleTarget->imageView, nodeInputs[0]->sampleTarget->inherited });
			nodeOutputs.sampleTarget->inherited = true;
		}
	}

	// framebuffer
	FrameGraphNode::renderPassReference = renderPassReference;
	framebuffers.resize(framesInFlightCount);
	for (int f = 0; f < framesInFlightCount; ++f)
	{
		std::vector<VkImageView> attachments;
		if (useSwapchain)
		{
			attachments.push_back(commonFrameData.swapchainImageViews[f]);
		}
		else
		{
			attachments.push_back(nodeOutputs.colorTarget->imageView);
		}

		if (nodeOutputs.depthTarget)
		{
			attachments.push_back(nodeOutputs.depthTarget->imageView);
		}

		if (nodeOutputs.sampleTarget)
		{
			attachments.push_back(nodeOutputs.sampleTarget->imageView);
		}

		// TODO: Figure out the target sizing.
		float widthModifier = nodeOutputCharacteristics.colorTarget ? nodeOutputCharacteristics.colorTarget->widthModifier : 1.f;
		float heightModifier = nodeOutputCharacteristics.colorTarget ? nodeOutputCharacteristics.colorTarget->heightModifier : 1.f;;
		framebuffers[f] = VulkanBackend::CreateFramebuffer(*backendData,
			(int)(commonFrameData.surfaceData->width * widthModifier), (int)(commonFrameData.surfaceData->height * heightModifier),
			renderPassReference, attachments);
	}

	// descriptors
	const int descriptorSetLayoutCount = 2;
	std::vector<UniformData> uniformData;
	ConfigureUniforms(nodeConfiguration["uniforms"], uniformData);
	ConfigureUniforms(nodeConfiguration["material"], materialData);

	uniformDescriptorSetLayout = DescriptorSystem::InitSetLayout(backendData, uniformData);
	uniformDescriptorSystem.Init(backendData, rendererData, uniformData, framesInFlightCount,
		uniformDescriptorSetLayout);

	materialDescriptorSetLayout = DescriptorSystem::InitSetLayout(backendData, materialData);

	// TODO: Model uniform set.
	// TODO: Attachment descriptor - I/O.

	// pipeline
	std::vector<VkDescriptorSetLayout> passSetLayouts
	{
		uniformDescriptorSetLayout,
		materialDescriptorSetLayout
	};

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)passSetLayouts.size();
	pipelineLayoutCreateInfo.pSetLayouts = passSetLayouts.data();
	VulkanCheck(vkCreatePipelineLayout(backendData->logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

	// TODO: Sample count.
	std::vector<VkDynamicState> dynamicStates =
	{
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_VIEWPORT
	};

	auto vertexAttributes = FrameGraphConfigurator::GetVertexAttributes(nodeConfiguration["vertex-attributes"]);
	std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions(vertexAttributes.size());
	for (int a = 0; a < vertexAttributes.size(); ++a)
	{
		vertexAttributeDescriptions[a].binding = 0;
		vertexAttributeDescriptions[a].format = PipelineUtils::GetAttributeFormat(vertexAttributes[a]);
		vertexAttributeDescriptions[a].location = a;
		vertexAttributeDescriptions[a].offset = vertexSize;
		vertexSize += vertexAttributes[a].byteCount;
	}

	const int descriptionsCount = 2;
	VkVertexInputBindingDescription vertexBindingDescriptions[descriptionsCount];
	vertexBindingDescriptions[0] = {};
	vertexBindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexBindingDescriptions[0].stride = vertexSize;
	vertexBindingDescriptions[0].binding = 0;

	vertexBindingDescriptions[1] = {};
	vertexBindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
	// model matrix will have 4*4*4 bytes
	vertexBindingDescriptions[1].stride = 64;
	vertexBindingDescriptions[1].binding = 1;

	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = descriptionsCount;
	vertexInput.pVertexBindingDescriptions = vertexBindingDescriptions;
	vertexInput.vertexAttributeDescriptionCount = (uint32_t)vertexAttributeDescriptions.size();
	vertexInput.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages(2);
	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].pName = "main";
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].pName = "main";

	auto shaders = FrameGraphConfigurator::GetShaders(nodeConfiguration["shaders"]);
	for (int s = 0; s < shaders.size(); ++s)
	{
		shaderStages[s].module = VulkanShaderCompiler::Compile(backendData->logicalDevice, shaders[s].second.c_str());
		shaderModules.push_back(shaderStages[s].module);
	}

	VkCullModeFlags cullMode = FrameGraphConfigurator::GetCullMode(nodeConfiguration["cull-mode"]);
	pipeline = VulkanBackend::CreateGraphicsPipeline(*backendData,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
		cullMode, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
		VK_SAMPLE_COUNT_1_BIT, dynamicStates, vertexInput, renderPassReference,
		pipelineLayout, shaderStages, commonFrameData.pipelineCache);
}

bool RasterizeNode::Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData,
	bool startRenderPass, bool endRenderPass)
{
	if (materialDescriptorSystems.empty())
	{
		return false;
	}

	if (startRenderPass)
	{
		// TODO: Base clear on the clear parameter.
		const int clearValueCount = 2;
		VkClearValue clearValues[clearValueCount];
		clearValues[0].color = { 0.f, 0.f, 0.f };
		clearValues[1].depthStencil = { 1.f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPassReference;
		renderPassBeginInfo.renderArea.extent.width = commonFrameData.surfaceData->width;
		renderPassBeginInfo.renderArea.extent.height = commonFrameData.surfaceData->height;
		renderPassBeginInfo.clearValueCount = clearValueCount - (nodeOutputs.depthTarget ? 0 : 1);
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = framebuffers[frameInFlight];

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

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

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	for (int m = 0; m < materialDescriptorSystems.size(); ++m)
	{
		const auto& drawBuffer = drawBuffers[m];

		std::vector<VkDescriptorSet> descriptorSets;
		descriptorSets.reserve(4);
		if (materialDescriptorSystems[m]->GetSet(frameInFlight) != VK_NULL_HANDLE)
		{
			descriptorSets.push_back(materialDescriptorSystems[m]->GetSet(frameInFlight));
		}
		if (uniformDescriptorSystem.GetSet(frameInFlight) != VK_NULL_HANDLE)
		{
			descriptorSets.push_back(uniformDescriptorSystem.GetSet(frameInFlight));
		}

		if (descriptorSets.size() > 0)
		{
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
				(uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		}

		// TODO: Instances.
		for (int b = 0; b < drawBuffer.size(); ++b)
		{
			static VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &drawBuffer[b].vertexBuffer->buffer.buffer, &offset);
			vkCmdBindVertexBuffers(commandBuffer, 1, 1, &drawBuffer[b].instanceBuffer->buffer.buffer, &offset);
			if (drawBuffer[b].indexBuffer)
			{
				vkCmdBindIndexBuffer(commandBuffer, drawBuffer[b].indexBuffer->buffer.buffer, offset, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(commandBuffer, drawBuffer[b].indexBuffer->dataSize / 4, drawBuffer[b].instanceBuffer->dataSize, 0, 0, 0);
			}
			else
			{
				vkCmdDraw(commandBuffer, drawBuffer[b].vertexBuffer->dataSize / vertexSize, drawBuffer[b].instanceBuffer->dataSize, 0, 0);
			}
		}
	}

	if (endRenderPass)
	{
		vkCmdEndRenderPass(commandBuffer);
	}

	return true;
}

void RasterizeNode::Resize(const CommonFrameData& commonFrameData)
{

}
