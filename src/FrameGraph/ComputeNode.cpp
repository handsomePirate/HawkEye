#include "ComputeNode.hpp"
#include "../Resources.hpp"
#include "../Descriptors.hpp"
#include "../Pipeline.hpp"
#include <VulkanBackend/ErrorCheck.hpp>
#include <VulkanShaderCompiler/VulkanShaderCompilerAPI.hpp>
#include <SoftwareCore/DefaultLogger.hpp>

ComputeNode::ComputeNode(const std::string& name, int framesInFlightCount, bool isFinal)
	: FrameGraphNode(name, framesInFlightCount, FrameGraphNodeType::Computed, isFinal) {}

ComputeNode::~ComputeNode()
{
}

void ComputeNode::Configure(const YAML::Node& nodeConfiguration,
	const std::vector<NodeOutputs*>& nodeInputs, std::vector<InputTargetCharacteristics>& inputCharacteristics,
	OutputTargetCharacteristics& outputCharacteristics,
	const CommonFrameData& commonFrameData, VkRenderPass renderPassReference, bool useSwapchain)
{
	backendData = commonFrameData.backendData;
	rendererData = commonFrameData.rendererData;

	// inputs & outputs
	nodeInputCharacteristics = std::move(inputCharacteristics);
	nodeOutputCharacteristics = std::move(outputCharacteristics);
	// Create new targets, iff no slot is connected or the node is read-write
	// targets

	// color
	bool reuseColorTarget = false;
	bool preserveColor = false;
	if (nodeInputCharacteristics.size() == 1 && nodeInputCharacteristics[0].colorTarget &&
		nodeOutputCharacteristics.colorTarget && nodeOutputCharacteristics.colorTarget->write &&
		!nodeOutputCharacteristics.colorTarget->read &&
		nodeInputCharacteristics[0].colorTarget->imageFormat.Equals(nodeOutputCharacteristics.colorTarget->imageFormat) &&
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
		nodeInputCharacteristics[0].depthTarget->imageFormat.Equals(nodeOutputCharacteristics.depthTarget->imageFormat) &&
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
		nodeInputCharacteristics[0].sampleTarget->imageFormat.Equals(nodeOutputCharacteristics.sampleTarget->imageFormat) &&
		nodeInputs.size() > 0)
		// TODO: nodeInputs also need to contain sample target.
	{
		reuseSampleTarget = true;
		if (nodeInputCharacteristics[0].sampleTarget->contentOperation == ContentOperation::Preserve)
		{
			preserveSample = true;
		}
	}

	this->useSwapchain = useSwapchain;
	this->reuseColorTarget = reuseColorTarget;
	this->reuseDepthTarget = reuseDepthTarget;
	this->reuseSampleTarget = reuseSampleTarget;

	// TODO: De-duplicate with rasterize node.
	CreateColorTarget(commonFrameData, nodeInputs);
	CreateDepthTarget(commonFrameData, nodeInputs);
	CreateSampleTarget(commonFrameData, nodeInputs);

	// framebuffer
	FrameGraphNode::renderPassReference = renderPassReference;

	// descriptors
	const int descriptorSetLayoutCount = 2;
	std::vector<UniformData> uniformData;
	ConfigureUniforms(nodeConfiguration["uniforms"], uniformData);
	ConfigureUniforms(nodeConfiguration["material"], materialData);

	uniformDescriptorSetLayout = DescriptorSystem::InitSetLayout(backendData, uniformData);
	uniformDescriptorSystem.Init(backendData, rendererData, uniformData, framesInFlightCount,
		uniformDescriptorSetLayout);

	//materialDescriptorSetLayout = DescriptorSystem::InitSetLayout(backendData, materialData);

	// TODO: Model uniform set.

	std::vector<UniformData> targetUniforms;
	targetUniforms.push_back({ "target image", 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT });

	if (nodeInputCharacteristics.size() > 0 && !reuseColorTarget)
	{
		targetUniforms.push_back({ "source image", 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT });
	}
	targetDescriptorSystemLayout = DescriptorSystem::InitSetLayout(backendData, targetUniforms);
	targetDescriptorSystem.Init(backendData, rendererData, targetUniforms, useSwapchain ? framesInFlightCount : 1, targetDescriptorSystemLayout);
	
	for (int i = 0; i < (useSwapchain ? framesInFlightCount : 1); ++i)
	{
		VkImageView imageView = useSwapchain ? commonFrameData.swapchainImageViews[i] : nodeOutputs.colorTarget->imageView;
		targetDescriptorSystem.UpdateStorageImage("target image", i, imageView);

		if (nodeInputCharacteristics.size() > 0 && !reuseColorTarget)
		{
			HawkEye::HTexture_t sourceImage;
			sourceImage.uploadFence = VK_NULL_HANDLE;
			sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			sourceImage.imageView = nodeInputs[0]->colorTarget->imageView;
			sourceImage.sampler = commonFrameData.targetSampler;
			targetDescriptorSystem.UpdateTexture("source image", i, &sourceImage);
		}
	}

	// pipeline
	std::vector<VkDescriptorSetLayout> passSetLayouts
	{
		targetDescriptorSystemLayout,
		uniformDescriptorSetLayout
	};

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)passSetLayouts.size();
	pipelineLayoutCreateInfo.pSetLayouts = passSetLayouts.data();
	VulkanCheck(vkCreatePipelineLayout(backendData->logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

	VkPipelineShaderStageCreateInfo shaderStage{};
	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStage.pName = "main";
	
	auto shaders = FrameGraphConfigurator::GetShaders(nodeConfiguration["shaders"]);
	shaderStage.module = VulkanShaderCompiler::Compile(backendData->logicalDevice, shaders[0].second.c_str());
	shaderModules.push_back(shaderStage.module);

	pipeline = VulkanBackend::CreateComputePipeline(*backendData, pipelineLayout, shaderStage, commonFrameData.pipelineCache);

	configured = true;
}

void ComputeNode::Shutdown(const CommonFrameData& commonFrameData)
{
	if (!configured)
	{
		return;
	}

	const VulkanBackend::BackendData& backendData = *commonFrameData.backendData;

	VulkanBackend::DestroyPipeline(backendData, pipeline);
	VulkanBackend::DestroyPipelineLayout(backendData, pipelineLayout);

	for (int s = 0; s < shaderModules.size(); ++s)
	{
		VulkanBackend::DestroyShaderModule(backendData, shaderModules[s]);
	}
	shaderModules.clear();

	VulkanBackend::DestroyDescriptorSetLayout(backendData, targetDescriptorSystemLayout);
	//VulkanBackend::DestroyDescriptorSetLayout(backendData, materialDescriptorSetLayout);
	VulkanBackend::DestroyDescriptorSetLayout(backendData, uniformDescriptorSetLayout);

	if (nodeOutputs.colorTarget)
	{
		FramebufferUtils::DestroyTarget(backendData, *nodeOutputs.colorTarget.get());
	}
	if (nodeOutputs.depthTarget)
	{
		FramebufferUtils::DestroyTarget(backendData, *nodeOutputs.depthTarget.get());
	}
	if (nodeOutputs.sampleTarget)
	{
		FramebufferUtils::DestroyTarget(backendData, *nodeOutputs.sampleTarget.get());
	}

	targetDescriptorSystem.Shutdown();
	uniformDescriptorSystem.Shutdown();

	for (int m = 0; m < materialDescriptorSystems.size(); ++m)
	{
		materialDescriptorSystems[m]->Shutdown();
	}
}

bool ComputeNode::Record(VkCommandBuffer commandBuffer, int frameInFlight, const CommonFrameData& commonFrameData,
	bool startRenderPass, bool endRenderPass)
{
	if (!configured)
	{
		CoreLogWarn(DefaultLogger, "Computed node (%s): trying to record a node that was not configured.", name.c_str());
		return false;
	}

	//if (materialDescriptorSystems.empty())
	//{
	//	return false;
	//}

	VkImage imageReference = useSwapchain ? commonFrameData.swapchainImages[frameInFlight] : nodeOutputs.colorTarget->image.image;

	VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		imageReference, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	std::vector<VkDescriptorSet> descriptorSets;
	descriptorSets.reserve(4);
	int setIndex = useSwapchain ? frameInFlight : 0;
	descriptorSets.push_back(targetDescriptorSystem.GetSet(setIndex));
	if (uniformDescriptorSystem.GetSet(frameInFlight) != VK_NULL_HANDLE)
	{
		descriptorSets.push_back(uniformDescriptorSystem.GetSet(frameInFlight));
	}
	if (descriptorSets.size() > 0)
	{
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0,
			(uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
	}

	// TODO: Works in multiples of 16, make sure that exactly the entire picture is rendered onto the screen.
	vkCmdDispatch(commandBuffer, (commonFrameData.surfaceData->width + 15) / 16, (commonFrameData.surfaceData->height + 15) / 16, 1);

	if (endRenderPass && !useSwapchain)
	{
		VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			imageReference, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);
	}

	if (isFinal && useSwapchain)
	{
		VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			imageReference, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);
	}

	return true;
}

void ComputeNode::Resize(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs)
{
	if (!configured)
	{
		CoreLogWarn(DefaultLogger, "Computed node (%s): trying to resize a node that was not configured.", name.c_str());
		return;
	}

	const VulkanBackend::BackendData& backendData = *commonFrameData.backendData;

	if (nodeOutputs.colorTarget)
	{
		FramebufferUtils::DestroyTarget(backendData, *nodeOutputs.colorTarget.get());
		CreateColorTarget(commonFrameData, nodeInputs);
	}
	if (nodeOutputs.depthTarget)
	{
		FramebufferUtils::DestroyTarget(backendData, *nodeOutputs.depthTarget.get());
		CreateDepthTarget(commonFrameData, nodeInputs);
	}
	if (nodeOutputs.sampleTarget)
	{
		FramebufferUtils::DestroyTarget(backendData, *nodeOutputs.sampleTarget.get());
		CreateSampleTarget(commonFrameData, nodeInputs);
	}

	for (int i = 0; i < (useSwapchain ? framesInFlightCount : 1); ++i)
	{
		VkImageView imageView = useSwapchain ? commonFrameData.swapchainImageViews[i] : nodeOutputs.colorTarget->imageView;
		targetDescriptorSystem.UpdateStorageImage("target image", i, imageView);

		if (nodeInputCharacteristics.size() > 0 && !reuseColorTarget)
		{
			HawkEye::HTexture_t sourceImage;
			sourceImage.uploadFence = VK_NULL_HANDLE;
			sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			sourceImage.imageView = nodeInputs[0]->colorTarget->imageView;
			sourceImage.sampler = commonFrameData.targetSampler;
			targetDescriptorSystem.UpdateTexture("source image", i, &sourceImage);
		}
	}
}

void ComputeNode::CreateColorTarget(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs)
{
	if (!useSwapchain && nodeOutputCharacteristics.colorTarget)
	{
		if (nodeOutputCharacteristics.colorTarget && !reuseColorTarget)
		{
			nodeOutputs.colorTarget = std::make_unique<Target>(FramebufferUtils::CreateColorTarget(*backendData,
				*commonFrameData.surfaceData.get(), nodeOutputCharacteristics.colorTarget->imageFormat));
		}
		else
		{
			nodeOutputs.colorTarget = std::make_unique<Target>(
				Target{ nodeInputs[0]->colorTarget->image, nodeInputs[0]->colorTarget->imageView, nodeInputs[0]->colorTarget->inherited });
			nodeOutputs.colorTarget->inherited = true;
		}
	}
}

void ComputeNode::CreateDepthTarget(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs)
{
	if (nodeOutputCharacteristics.depthTarget)
	{
		if (!reuseDepthTarget)
		{
			nodeOutputs.depthTarget = std::make_unique<Target>(FramebufferUtils::CreateDepthTarget(*backendData,
				*commonFrameData.surfaceData.get(), nodeOutputCharacteristics.depthTarget->imageFormat));
		}
		else
		{
			nodeOutputs.depthTarget = std::make_unique<Target>(
				Target{ nodeInputs[0]->depthTarget->image, nodeInputs[0]->depthTarget->imageView, nodeInputs[0]->depthTarget->inherited });
			nodeOutputs.depthTarget->inherited = true;
		}
	}
}

void ComputeNode::CreateSampleTarget(const CommonFrameData& commonFrameData, const std::vector<NodeOutputs*>& nodeInputs)
{
	// TODO: Sample target in render passes.
	if (nodeOutputCharacteristics.sampleTarget)
	{
		if (!reuseSampleTarget)
		{
			nodeOutputs.sampleTarget = std::make_unique<Target>(FramebufferUtils::CreateColorTarget(*backendData,
				*commonFrameData.surfaceData.get(), nodeOutputCharacteristics.sampleTarget->imageFormat));
		}
		else
		{
			nodeOutputs.sampleTarget = std::make_unique<Target>(
				Target{ nodeInputs[0]->sampleTarget->image, nodeInputs[0]->sampleTarget->imageView, nodeInputs[0]->sampleTarget->inherited });
			nodeOutputs.sampleTarget->inherited = true;
		}
	}
}
