#include "Pipeline.hpp"
#include "Resources.hpp"
#include "Descriptors.hpp"
#include "Commands.hpp"
#include "Framebuffer.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/ErrorCheck.hpp>
#define CompilerLogger VulkanLogger
#include <VulkanShaderCompiler/Logger.hpp>
#include <VulkanShaderCompiler/VulkanShaderCompilerAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>
#include <memory>
#include <chrono>

HawkEye::Pipeline::Pipeline()
	: p_(new Private) {}

HawkEye::Pipeline::~Pipeline()
{
	delete p_;
}

bool HawkEye::Pipeline::Configured() const
{
	return p_->configured;
}

void HawkEye::Pipeline::Configure(HRendererData rendererData, const char* configFile, int width, int height,
	void* windowHandle, void* windowConnection)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	YAML::Node configData = YAML::LoadFile(configFile);

	auto& passes = p_->passes;
	if (configData["Pipeline"])
	{
		ConfigureCommon(configData["Pipeline"], p_->pipelineTargets, p_->samples);

		if (configData["Pipeline"]["passes"])
		{
			if (!configData["Pipeline"]["passes"].IsSequence())
			{
				CoreLogError(VulkanLogger, "Pipeline: Wrong format for passes.");
				return;
			}

			ConfigureUniforms(configData["Pipeline"]["uniforms"], p_->uniformInfo.uniforms);
			for (int l = 0; l < configData["Pipeline"]["passes"].size(); ++l)
			{
				passes.push_back(ConfigureLayer(configData["Pipeline"]["passes"][l]));
			}
		}
		else
		{
			CoreLogWarn(VulkanLogger, "Pipeline: Configuration missing passes.");
		}
	}
	else
	{
		CoreLogError(VulkanLogger, "Pipeline: Missing configuration.");
		return;
	}

	// TODO: Make offscreen rendering supported.
	if (!windowHandle || !windowConnection)
	{
		CoreLogError(VulkanLogger, "Pipeline: Offscreen rendering currently not supported. Please provide a window.");
		return;
	}

	p_->backendData = (VulkanBackend::BackendData*)rendererData;

	p_->surfaceData = std::make_unique<VulkanBackend::SurfaceData>();
	VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
	surfaceData.width = width;
	surfaceData.height = height;
	VkDevice device = backendData.logicalDevice;

	p_->passData.resize(passes.size());

	int retargetChainLength = 0;
	for (int p = 0; p < passes.size(); ++p)
	{
		if (passes[p].type == PipelinePass::Type::Computed)
		{
			p_->containsComputedPass = true;
			if (p > 0)
			{
				++retargetChainLength;
			}
		}
		p_->passData[p].type = passes[p].type;
		p_->passData[p].colorTarget = retargetChainLength;
	}
	++retargetChainLength;

	if (windowHandle)
	{
		VulkanBackend::CreateSurface(backendData, surfaceData, windowHandle, windowConnection);
		VulkanBackend::GetDepthFormat(backendData, surfaceData);
		VulkanBackend::GetSurfaceFormat(backendData, surfaceData);
		VulkanBackend::GetSurfaceCapabilities(backendData, surfaceData);
		VulkanBackend::GetSurfaceExtent(backendData, surfaceData);
		VulkanBackend::GetPresentMode(backendData, surfaceData);
		VulkanBackend::GetSwapchainImageCount(surfaceData);

		VulkanBackend::FilterPresentQueues(backendData, surfaceData);

		VulkanBackend::SelectPresentQueue(backendData, surfaceData);
		VulkanBackend::SelectPresentComputeQueue(backendData, surfaceData);

		if (p_->containsComputedPass)
		{
			p_->swapchain = VulkanBackend::CreateSwapchain(backendData, surfaceData,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		}
		else
		{
			p_->swapchain = VulkanBackend::CreateSwapchain(backendData, surfaceData);
		}

		VulkanBackend::GetSwapchainImages(backendData, p_->swapchain, p_->swapchainImages);

		p_->swapchainImageViews.resize(p_->swapchainImages.size());
		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			p_->swapchainImageViews[i] = VulkanBackend::CreateImageView2D(backendData, p_->swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	p_->phases.resize(retargetChainLength);
	for (int t = 0; t < p_->phases.size() - 1; ++t)
	{
		p_->phases[t].colorTarget = FramebufferUtils::CreateColorTarget(backendData, surfaceData, true);
	}

	if (p_->phases.size() > 1)
	{
		p_->targetSampler = VulkanBackend::CreateImageSampler(backendData, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			VK_BORDER_COLOR_INT_TRANSPARENT_BLACK, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.f, 1);
	}

	// TODO: Remove color target from these.
	p_->targets.resize(p_->pipelineTargets.size());
	for (int t = 0; t < p_->pipelineTargets.size(); ++t)
	{
		if (p_->pipelineTargets[t] == PipelineTarget::Depth)
		{
			p_->targets[(int)PipelineTarget::Depth] = FramebufferUtils::CreateDepthTarget(backendData, surfaceData);
			p_->hasDepthTarget = true;
		}
	}

	// TODO: 2^2 render pass possibilities - create only the ones needed.
	p_->renderPassUP = VulkanBackend::CreateRenderPass(backendData, surfaceData, p_->hasDepthTarget);
	p_->renderPassUA = VulkanBackend::CreateRenderPass(backendData, surfaceData, p_->hasDepthTarget,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	p_->renderPassGA = VulkanBackend::CreateRenderPass(backendData, surfaceData, p_->hasDepthTarget,
		VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	p_->renderPassGP = VulkanBackend::CreateRenderPass(backendData, surfaceData, p_->hasDepthTarget,
		VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	for (int t = 0; t < p_->phases.size(); ++t)
	{
		VkRenderPass renderPass;
		if (p_->phases.size() == 1 && !p_->containsComputedPass)
		{
			renderPass = p_->renderPassUP;
			p_->phases[t].type = PhaseType::UP;
		}
		else if (t == 0 && p_->passData[0].type == PipelinePass::Type::Rasterized)
		{
			renderPass = p_->renderPassUA;
			p_->phases[t].type = PhaseType::UA;
		}
		else if (t == p_->phases.size() - 1)
		{
			renderPass = p_->renderPassGP;
			p_->phases[t].type = PhaseType::GP;
		}
		else
		{
			renderPass = p_->renderPassGA;
			p_->phases[t].type = PhaseType::GA;
		}
		p_->phases[t].renderPass = renderPass;
		p_->phases[t].framebuffers.resize(p_->swapchainImageViews.size());
		for (int v = 0; v < p_->swapchainImageViews.size(); ++v)
		{
			std::vector<VkImageView> attachments;
			if (t == p_->phases.size() - 1)
			{
				attachments.push_back(p_->swapchainImageViews[v]);
			}
			else
			{
				attachments.push_back(p_->phases[t].colorTarget.imageView);
			}
			if (p_->hasDepthTarget)
			{
				attachments.push_back(p_->targets[(int)PipelineTarget::Depth].imageView);
			}

			p_->phases[t].framebuffers[v] = VulkanBackend::CreateFramebuffer(backendData, width, height, renderPass, attachments);
		}
	}

	VkPipelineCache pipelineCache = VulkanBackend::CreatePipelineCache(backendData);
	p_->pipelineCache = pipelineCache;

	p_->frameData.resize(p_->swapchainImageViews.size());

	for (int p = 0; p < passes.size(); ++p)
	{
		p_->passData[p].inheritDepth = passes[p].inheritDepth;
		if (p == 0 && passes[p].inheritDepth)
		{
			CoreLogInfo(VulkanLogger, "Pipeline pass: Inheriting depth for the first pass is meaningless.");
		}
		if (!passes[p].material.empty())
		{
			for (int u = 0; u < passes[p].material.size(); ++u)
			{
				if (passes[p].material[u].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				{
					p_->passData[p].materialSize += 8;
				}
				else
				{
					p_->passData[p].materialSize += passes[p].material[u].size;
				}
			}
			p_->passData[p].materialData = passes[p].material;

			p_->passData[p].descriptorSetLayouts.push_back(DescriptorUtils::GetSetLayout(backendData, p_->passData[p].materialData));
		}
		if (!passes[p].uniforms.empty())
		{
			VkDescriptorSetLayout descriptorSetLayout = DescriptorUtils::GetSetLayout(backendData, passes[p].uniforms);
			p_->passData[p].passUniformLayouts.push_back(descriptorSetLayout);

			auto poolSizes = DescriptorUtils::GetPoolSizes(rendererData, passes[p].uniforms, HawkEye::BufferType::Mapped, p_->passData[p].uniformBuffers);

			// TODO: Materials should not be storage buffers.
			DescriptorUtils::UpdateSets(rendererData, backendData, passes[p].uniforms, poolSizes, p_->passData[p].descriptorData.descriptorPool,
				p_->passData[p].descriptorData.descriptorSet, descriptorSetLayout, p_->passData[p].uniformBuffers, p_->passData[p].uniformTextureBindings,
				p_->passData[p].storageBufferBindings);
		}
	}

	VkDescriptorSetLayout commonDescriptorSetLayout = VK_NULL_HANDLE;
	if (!p_->uniformInfo.uniforms.empty())
	{
		commonDescriptorSetLayout = DescriptorUtils::GetSetLayout(backendData, p_->uniformInfo.uniforms);

		auto poolSizes = DescriptorUtils::GetPoolSizes(rendererData, p_->uniformInfo.uniforms, HawkEye::BufferType::Mapped, p_->uniformBuffers);

		DescriptorUtils::UpdateSets(rendererData, backendData, p_->uniformInfo.uniforms, poolSizes, p_->descriptorData.descriptorPool,
			p_->descriptorData.descriptorSet, commonDescriptorSetLayout, p_->uniformBuffers, p_->uniformTextureBindings, p_->storageBufferBindings);
	}

	for (int p = 0; p < p_->passData.size(); ++p)
	{
		std::vector<UniformData> frameUniforms;
		frameUniforms.push_back({ "time", 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL });
		std::vector<VkDescriptorPoolSize> poolSizes;

		if (p_->passData[p].type == PipelinePass::Type::Computed)
		{
			frameUniforms.push_back({ "target image", 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT });

			if (p > 0 && (p_->phases[p_->passData[p].colorTarget].type == PhaseType::GA ||
				p_->phases[p_->passData[p].colorTarget].type == PhaseType::GP))
			{
				frameUniforms.push_back({ "source image", 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT });
			}

			poolSizes.resize(frameUniforms.size());
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			poolSizes[1].descriptorCount = 1;
			if (poolSizes.size() > 2)
			{
				poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				poolSizes[2].descriptorCount = 1;
			}
		}
		else
		{
			poolSizes.resize(frameUniforms.size());
		}
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;

		p_->passData[p].frameDescriptors.resize(p_->swapchainImages.size());
		p_->passData[p].frameDescriptorLayout = DescriptorUtils::GetSetLayout(backendData, frameUniforms);
		p_->passData[p].frameDecriptorBuffers.resize(p_->swapchainImages.size());

		for (int c = 0; c < p_->frameData.size(); ++c)
		{
			p_->passData[p].frameDescriptors[c].descriptorPool = VulkanBackend::CreateDescriptorPool(backendData, poolSizes, 1);
			p_->passData[p].frameDescriptors[c].descriptorSet = VulkanBackend::AllocateDescriptorSet(backendData,
				p_->passData[p].frameDescriptors[c].descriptorPool, p_->passData[p].frameDescriptorLayout);
			p_->passData[p].frameDecriptorBuffers[c] = HawkEye::UploadBuffer(rendererData, nullptr, sizeof(unsigned int), HawkEye::BufferUsage::Uniform,
				HawkEye::BufferType::Mapped);

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = p_->passData[p].frameDecriptorBuffers[c]->buffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(unsigned int);

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = p_->passData[p].frameDescriptors[c].descriptorSet;
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pBufferInfo = &bufferInfo;
			vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
		}

		if (p_->passData[p].type == PipelinePass::Type::Computed)
		{
			for (int c = 0; c < p_->frameData.size(); ++c)
			{
				VkDescriptorImageInfo imageInfo{};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				if (p_->passData[p].colorTarget >= p_->phases.size() - 1)
				{
					imageInfo.imageView = p_->swapchainImageViews[c];
				}
				else
				{
					imageInfo.imageView = p_->phases[p_->passData[p].colorTarget].colorTarget.imageView;
				}

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = p_->passData[p].frameDescriptors[c].descriptorSet;
				descriptorWrite.dstBinding = 1;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pImageInfo = &imageInfo;
				vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);

				if (p > 0 && (p_->phases[p_->passData[p].colorTarget].type == PhaseType::GA ||
					p_->phases[p_->passData[p].colorTarget].type == PhaseType::GP))
				{
					imageInfo = {};
					imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					imageInfo.imageView = p_->phases[p_->passData[p].colorTarget - 1].colorTarget.imageView;
					imageInfo.sampler = p_->targetSampler;

					descriptorWrite.dstBinding = 2;
					descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					descriptorWrite.pImageInfo = &imageInfo;
					vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
				}
			}
		}
	}

	for (int p = 0; p < passes.size(); ++p)
	{
		std::vector<VkDescriptorSetLayout> passSetLayouts = p_->passData[p].descriptorSetLayouts;
		if (p_->passData[p].type == PipelinePass::Type::Computed)
		{
			passSetLayouts.push_back(p_->passData[p].frameDescriptorLayout);
		}
		if (commonDescriptorSetLayout != VK_NULL_HANDLE)
		{
			passSetLayouts.push_back(commonDescriptorSetLayout);
		}
		for (int l = 0; l < p_->passData[p].passUniformLayouts.size(); ++l)
		{
			passSetLayouts.push_back(p_->passData[p].passUniformLayouts[l]);
		}
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
		pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)passSetLayouts.size();
		pipelineLayoutCreateInfo.pSetLayouts = passSetLayouts.data();
		VkPipelineLayout pipelineLayout;
		VulkanCheck(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
		p_->passData[p].pipelineLayout = pipelineLayout;

		if (passes[p].type == PipelinePass::Type::Rasterized)
		{
			// TODO: Sample count.
			std::vector<VkDynamicState> dynamicStates =
			{
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_VIEWPORT
			};

			std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions(passes[p].attributes.size() + 4);
			int vertexSize = 0;
			for (int a = 0; a < passes[p].attributes.size(); ++a)
			{
				vertexAttributeDescriptions[a].binding = 0;
				vertexAttributeDescriptions[a].format = PipelineUtils::GetAttributeFormat(passes[p].attributes[a]);
				vertexAttributeDescriptions[a].location = a;
				vertexAttributeDescriptions[a].offset = vertexSize;
				vertexSize += passes[p].attributes[a].byteCount;
			}
			p_->passData[p].vertexSize = vertexSize;
			for (int a = (int)passes[p].attributes.size(); a < passes[p].attributes.size() + 4; ++a)
			{
				vertexAttributeDescriptions[a].binding = 1;
				vertexAttributeDescriptions[a].format = VK_FORMAT_R32G32B32A32_SFLOAT;
				vertexAttributeDescriptions[a].location = a;
				vertexAttributeDescriptions[a].offset = (uint32_t)(a - passes[p].attributes.size()) * 16;
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

			for (int s = 0; s < passes[p].shaders.size(); ++s)
			{
				shaderStages[s].module = VulkanShaderCompiler::Compile(device, passes[p].shaders[s].second.c_str());
				p_->passData[p].shaderModules.push_back(shaderStages[s].module);
			}

			p_->passData[p].rasterizationPipeline = VulkanBackend::CreateGraphicsPipeline(backendData,
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
				passes[p].cullMode, VK_FRONT_FACE_COUNTER_CLOCKWISE,
				VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
				VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
				VK_SAMPLE_COUNT_1_BIT, dynamicStates, vertexInput, p_->phases[p_->passData[p].colorTarget].renderPass,
				pipelineLayout, shaderStages, pipelineCache);
		}
		else
		{
			VkPipelineShaderStageCreateInfo shaderStage{};
			shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			shaderStage.pName = "main";
			shaderStage.module = VulkanShaderCompiler::Compile(device, passes[p].shaders[0].second.c_str());
			p_->passData[p].shaderModules.push_back(shaderStage.module);

			p_->passData[p].computePipeline = VulkanBackend::CreateComputePipeline(backendData,
				pipelineLayout, shaderStage, pipelineCache);
		}
	}

	p_->graphicsQueue = backendData.generalQueues[0];

	p_->graphicsSemaphore = VulkanBackend::CreateSemaphore(backendData);
	p_->presentSemaphore = VulkanBackend::CreateSemaphore(backendData);
	for (int v = 0; v < p_->swapchainImageViews.size(); ++v)
	{
		p_->frameFences.push_back(VulkanBackend::CreateFence(backendData, VK_FENCE_CREATE_SIGNALED_BIT));
	}

	p_->commandPool = VulkanBackend::CreateCommandPool(backendData, backendData.generalFamilyIndex,
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	std::vector<VkCommandBuffer> commandBuffers(p_->swapchainImageViews.size());

	VulkanBackend::AllocateCommandBuffers(backendData, p_->commandPool, commandBuffers.data(), (uint32_t)p_->swapchainImageViews.size());

	for (int c = 0; c < p_->frameData.size(); ++c)
	{
		p_->frameData[c].commandBuffer = commandBuffers[c];
		//CommandUtils::Record(c, backendData, p_);
	}

	VulkanBackend::DestroyDescriptorSetLayout(backendData, commonDescriptorSetLayout);

	p_->configured = true;

	CoreLogInfo(VulkanLogger, "Pipeline: Configuration successful.");
}

void HawkEye::Pipeline::Shutdown()
{
	if (p_->backendData)
	{
		const VulkanBackend::BackendData& backendData = *p_->backendData;
		VkDevice device = backendData.logicalDevice;
		vkDeviceWaitIdle(device);

		for (int p = 0; p < p_->passes.size(); ++p)
		{
			if (p_->passData[p].rasterizationPipeline)
			{
				VulkanBackend::DestroyPipeline(backendData, p_->passData[p].rasterizationPipeline);
			}
			if (p_->passData[p].computePipeline)
			{
				VulkanBackend::DestroyPipeline(backendData, p_->passData[p].computePipeline);
			}

			for (int s = 0; s < p_->passData[p].shaderModules.size(); ++s)
			{
				VulkanBackend::DestroyShaderModule(backendData, p_->passData[p].shaderModules[s]);
			}
			p_->passData[p].shaderModules.clear();

			if (p_->passData[p].pipelineLayout)
			{
				VulkanBackend::DestroyPipelineLayout(backendData, p_->passData[p].pipelineLayout);
			}
			for (int l = 0; l < p_->passData[p].passUniformLayouts.size(); ++l)
			{
				VulkanBackend::DestroyDescriptorSetLayout(backendData, p_->passData[p].passUniformLayouts[l]);
			}
		}
		if (p_->pipelineCache)
		{
			VulkanBackend::DestroyPipelineCache(backendData, p_->pipelineCache);
		}

		for (int t = 0; t < p_->phases.size(); ++t)
		{
			for (int f = 0; f < p_->phases[t].framebuffers.size(); ++f)
			{
				VulkanBackend::DestroyFramebuffer(backendData, p_->phases[t].framebuffers[f]);
			}
			FramebufferUtils::DestroyTarget(backendData, p_->phases[t].colorTarget);
		}
		p_->phases.clear();
		VulkanBackend::DestroyImageSampler(backendData, p_->targetSampler);

		VulkanBackend::DestroyRenderPass(backendData, p_->renderPassUP);
		VulkanBackend::DestroyRenderPass(backendData, p_->renderPassUA);
		VulkanBackend::DestroyRenderPass(backendData, p_->renderPassGA);
		VulkanBackend::DestroyRenderPass(backendData, p_->renderPassGP);
		if (p_->swapchain)
		{
			for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
			{
				VulkanBackend::DestroyImageView(backendData, p_->swapchainImageViews[i]);
			}
			p_->swapchainImageViews.clear();

			VulkanBackend::DestroySwapchain(backendData, p_->swapchain);
		}
		if (p_->surfaceData->surface)
		{
			VulkanBackend::DestroySurface(backendData, p_->surfaceData->surface);
		}

		if (p_->graphicsSemaphore)
		{
			VulkanBackend::DestroySemaphore(backendData, p_->graphicsSemaphore);
		}
		if (p_->presentSemaphore)
		{
			VulkanBackend::DestroySemaphore(backendData, p_->presentSemaphore);
		}
		for (int f = 0; f < p_->frameFences.size(); ++f)
		{
			VulkanBackend::DestroyFence(backendData, p_->frameFences[f]);
		}

		for (int t = 0; t < p_->targets.size(); ++t)
		{
			FramebufferUtils::DestroyTarget(backendData, p_->targets[t]);
		}
		for (int p = 0; p < p_->passes.size(); ++p)
		{
			for (int l = 0; l < p_->passData[p].descriptorSetLayouts.size(); ++l)
			{
				VulkanBackend::DestroyDescriptorSetLayout(backendData, p_->passData[p].descriptorSetLayouts[l]);
			}
			for (int m = 0; m < p_->passData[p].materials.size(); ++m)
			{
				VulkanBackend::DestroyDescriptorPool(backendData, p_->passData[p].materials[m].descriptorPool);
			}
			if (p_->passData[p].frameDescriptorLayout != VK_NULL_HANDLE)
			{
				VulkanBackend::DestroyDescriptorSetLayout(backendData, p_->passData[p].frameDescriptorLayout);
			}
			VulkanBackend::DestroyDescriptorPool(backendData, p_->passData[p].descriptorData.descriptorPool);
			for (auto& buffer : p_->passData[p].materialBuffers)
			{
				HawkEye::DeleteBuffer((HawkEye::HRendererData)p_->backendData, buffer.second);
			}
			for (auto& uniform : p_->passData[p].uniformBuffers)
			{
				HawkEye::DeleteBuffer((HawkEye::HRendererData)p_->backendData, uniform.second);
			}
		}
		VulkanBackend::DestroyDescriptorPool(backendData, p_->descriptorData.descriptorPool);
		for (auto& uniform : p_->uniformBuffers)
		{
			HawkEye::DeleteBuffer((HawkEye::HRendererData)p_->backendData, uniform.second);
		}

		for (int p = 0; p < p_->passData.size(); ++p)
		{
			for (int d = 0; d < p_->passData[p].frameDescriptors.size(); ++d)
			{
				VulkanBackend::DestroyDescriptorPool(backendData, p_->passData[p].frameDescriptors[d].descriptorPool);
				HawkEye::DeleteBuffer((HRendererData)p_->backendData, p_->passData[p].frameDecriptorBuffers[d]);
			}
		}
		for (int c = 0; c < p_->frameData.size(); ++c)
		{
			VulkanBackend::FreeCommandBuffer(backendData, p_->commandPool, p_->frameData[c].commandBuffer);
		}
		if (p_->commandPool)
		{
			VulkanBackend::DestroyCommandPool(backendData, p_->commandPool);
		}
	}
}

void HawkEye::Pipeline::UseBuffers(DrawBuffer* drawBuffers, int bufferCount, int pass)
{
	if (pass >= p_->passes.size() || pass < 0 || p_->passData[pass].type == PipelinePass::Type::Computed)
	{
		CoreLogError(VulkanLogger, "Pipeline: Invalid pass for draw buffer usage.");
		return;
	}

	p_->passData[pass].empty = true;
	p_->passData[pass].drawBuffers.clear();
	if (drawBuffers && bufferCount > 0)
	{
		for (int b = 0; b < bufferCount; ++b)
		{
			if (drawBuffers[b].material >= p_->passData[pass].materials.size() || drawBuffers[b].material < 0)
			{
				CoreLogError(VulkanLogger, "Pipeline: Invalid material passed for draw buffer %i.", b);
				continue;
			}

			if (drawBuffers[b].vertexBuffer->firstUse)
			{
				WaitForUpload((HRendererData)p_->backendData, drawBuffers[b].vertexBuffer);
			}
			if (drawBuffers[b].indexBuffer && drawBuffers[b].indexBuffer->firstUse)
			{
				WaitForUpload((HRendererData)p_->backendData, drawBuffers[b].indexBuffer);
			}

			p_->passData[pass].drawBuffers[drawBuffers[b].material].push_back(drawBuffers[b]);
			p_->passData[pass].empty = false;
		}
	}

	for (int c = 0; c < p_->frameData.size(); ++c)
	{
		p_->frameData[c].dirty = true;
	}
}

void HawkEye::Pipeline::DrawFrame()
{
	if (!p_->backendData || p_->surfaceData->width == 0 || p_->surfaceData->height == 0)
	{
		return;
	}
	assert(p_->configured);

	const VulkanBackend::BackendData& backendData = *p_->backendData;
	VkDevice device = backendData.logicalDevice;

	uint32_t frameIndex = (uint32_t)(p_->currentFrame % p_->frameFences.size());

	uint32_t currentImageIndex;
	VkResult result = vkAcquireNextImageKHR(device, p_->swapchain, UINT64_MAX, p_->presentSemaphore,
		VK_NULL_HANDLE, &currentImageIndex);
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
	{
		CoreLogWarn(VulkanLogger, "Pipeline: Should resize.");
		return;
	}
	VulkanCheck(result);

	vkWaitForFences(device, 1, &p_->frameFences[frameIndex], VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &p_->frameFences[frameIndex]);

	if (p_->frameData[currentImageIndex].dirty)
	{
		VulkanBackend::ResetCommandBuffer(p_->frameData[currentImageIndex].commandBuffer);

		CommandUtils::Record(currentImageIndex, backendData, p_);
	}

	auto millisecondsSinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	unsigned int concatTime = unsigned int(millisecondsSinceEpoch);
	// TODO: Does not need to be set for every pass, every frame is enough.
	for (int p = 0; p < p_->passData.size(); ++p)
	{
		HawkEye::UpdateBuffer((HRendererData)p_->backendData, p_->passData[p].frameDecriptorBuffers[currentImageIndex],
			&concatTime, sizeof(unsigned int));
	}

	static VkPipelineStageFlags pipelineStageWait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.commandBufferCount = 1;
	submitInfo.pWaitDstStageMask = &pipelineStageWait;
	submitInfo.pWaitSemaphores = &p_->presentSemaphore;
	submitInfo.pSignalSemaphores = &p_->graphicsSemaphore;
	submitInfo.pCommandBuffers = &p_->frameData[currentImageIndex].commandBuffer;

	VulkanCheck(vkQueueSubmit(p_->graphicsQueue, 1, &submitInfo, p_->frameFences[frameIndex]));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &p_->swapchain;
	presentInfo.pImageIndices = &currentImageIndex;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &p_->graphicsSemaphore;
	result = vkQueuePresentKHR(p_->surfaceData->defaultPresentQueue, &presentInfo);
	if (!((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR)))
	{
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			// Swap chain is no longer compatible with the surface and needs to be recreated
			CoreLogWarn(VulkanLogger, "Pipeline: Should resize.");
			return;
		}
		else
		{
			VulkanCheck(result);
		}
	}

	++p_->currentFrame;
}

void HawkEye::Pipeline::Resize(int width, int height)
{
	if (!p_->backendData || width == 0 || height == 0)
	{
		VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
		surfaceData.width = width;
		surfaceData.height = height;
		return;
	}

	assert(p_->configured);

	const VulkanBackend::BackendData& backendData = *p_->backendData;
	VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
	VkDevice device = p_->backendData->logicalDevice;

	vkDeviceWaitIdle(device);

	for (int t = 0; t < p_->phases.size(); ++t)
	{
		for (int f = 0; f < p_->phases[t].framebuffers.size(); ++f)
		{
			VulkanBackend::DestroyFramebuffer(backendData, p_->phases[t].framebuffers[f]);
		}
	}

	for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
	{
		VulkanBackend::DestroyImageView(backendData, p_->swapchainImageViews[i]);
	}

	for (int t = 0; t < p_->phases.size(); ++t)
	{
		FramebufferUtils::DestroyTarget(backendData, p_->phases[t].colorTarget);
	}
	for (int t = 0; t < p_->targets.size(); ++t)
	{
		if (p_->targets[t].image.image != VK_NULL_HANDLE)
		{
			FramebufferUtils::DestroyTarget(backendData, p_->targets[t]);
		}
	}

	if (p_->surfaceData->surface)
	{
		VulkanBackend::GetSurfaceCapabilities(backendData, surfaceData);
		VulkanBackend::GetSurfaceExtent(backendData, surfaceData);

		surfaceData.width = surfaceData.surfaceExtent.width;
		surfaceData.height = surfaceData.surfaceExtent.height;

		if (p_->containsComputedPass)
		{
			p_->swapchain = VulkanBackend::RecreateSwapchain(*p_->backendData, surfaceData, p_->swapchain,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		}
		else
		{
			p_->swapchain = VulkanBackend::RecreateSwapchain(*p_->backendData, surfaceData, p_->swapchain);
		}

		p_->swapchainImages.clear();
		VulkanBackend::GetSwapchainImages(backendData, p_->swapchain, p_->swapchainImages);

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			p_->swapchainImageViews[i] = VulkanBackend::CreateImageView2D(backendData, p_->swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	for (int t = 0; t < p_->phases.size() - 1; ++t)
	{
		p_->phases[t].colorTarget = FramebufferUtils::CreateColorTarget(backendData, surfaceData, true);
	}
	if (p_->hasDepthTarget == true)
	{
		p_->targets[(int)PipelineTarget::Depth] = FramebufferUtils::CreateDepthTarget(backendData, surfaceData);
	}

	for (int t = 0; t < p_->phases.size(); ++t)
	{
		for (int v = 0; v < p_->swapchainImageViews.size(); ++v)
		{
			std::vector<VkImageView> attachments;
			if (t == p_->phases.size() - 1)
			{
				attachments.push_back(p_->swapchainImageViews[v]);
			}
			else
			{
				attachments.push_back(p_->phases[t].colorTarget.imageView);
			}
			if (p_->hasDepthTarget)
			{
				attachments.push_back(p_->targets[(int)PipelineTarget::Depth].imageView);
			}
			p_->phases[t].framebuffers[v] = VulkanBackend::CreateFramebuffer(backendData, surfaceData.width, surfaceData.height,
				p_->phases[t].renderPass, attachments);
		}
	}

	for (int p = 0; p < p_->passData.size(); ++p)
	{
		if (p_->passData[p].type == PipelinePass::Type::Computed)
		{
			for (int c = 0; c < p_->frameData.size(); ++c)
			{
				VkDescriptorImageInfo imageInfo{};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				if (p_->passData[p].colorTarget >= p_->phases.size() - 1)
				{
					imageInfo.imageView = p_->swapchainImageViews[c];
				}
				else
				{
					imageInfo.imageView = p_->phases[p_->passData[p].colorTarget].colorTarget.imageView;
				}

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = p_->passData[p].frameDescriptors[c].descriptorSet;
				descriptorWrite.dstBinding = 1;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pImageInfo = &imageInfo;
				vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);

				if (p > 0 && (p_->phases[p_->passData[p].colorTarget].type == PhaseType::GA ||
					p_->phases[p_->passData[p].colorTarget].type == PhaseType::GP))
				{
					imageInfo = {};
					imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					imageInfo.imageView = p_->phases[p_->passData[p].colorTarget - 1].colorTarget.imageView;
					imageInfo.sampler = p_->targetSampler;

					descriptorWrite.dstBinding = 2;
					descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					descriptorWrite.pImageInfo = &imageInfo;
					vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
				}
			}
		}
	}

	for (int c = 0; c < p_->frameData.size(); ++c)
	{
		p_->frameData[c].dirty = true;
	}
}

void HawkEye::Pipeline::Refresh()
{
	for (int c = 0; c < p_->frameData.size(); ++c)
	{
		p_->frameData[c].dirty = true;
	}
}

void HawkEye::Pipeline::ReleaseResources()
{
	vkDeviceWaitIdle(p_->backendData->logicalDevice);

	for (int c = 0; c < p_->frameData.size(); ++c)
	{
		VulkanBackend::ResetCommandPool(*p_->backendData, p_->commandPool);
	}
}

uint64_t HawkEye::Pipeline::GetPresentedFrame() const
{
	return p_->currentFrame;
}

uint64_t HawkEye::Pipeline::GetFramesInFlight() const
{
	return p_->frameFences.size();
}

uint64_t HawkEye::Pipeline::GetUUID() const
{
	return (uint64_t)this;
}

void HawkEye::Pipeline::SetUniform(const std::string& name, HTexture texture)
{
	auto it = p_->uniformTextureBindings.find(name);
	if (it == p_->uniformTextureBindings.end())
	{
		CoreLogError(VulkanLogger, "Pipeline: Uniform texture with name %s does not exist.", name.c_str());
		return;
	}

	WaitForUpload((HawkEye::HRendererData)p_->backendData, texture);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = texture->imageLayout;
	imageInfo.imageView = texture->imageView;
	imageInfo.sampler = texture->sampler;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = p_->descriptorData.descriptorSet;
	descriptorWrite.dstBinding = p_->uniformTextureBindings[name];
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(p_->backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void HawkEye::Pipeline::SetUniform(const std::string& name, HTexture texture, int pass)
{
	auto it = p_->passData[pass].uniformTextureBindings.find(name);
	if (it == p_->passData[pass].uniformTextureBindings.end())
	{
		CoreLogError(VulkanLogger, "Pipeline: Uniform texture binding with name %s does not exist.", name.c_str());
		return;
	}

	WaitForUpload((HawkEye::HRendererData)p_->backendData, texture);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = texture->imageLayout;
	imageInfo.imageView = texture->imageView;
	imageInfo.sampler = texture->sampler;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = p_->passData[pass].descriptorData.descriptorSet;
	descriptorWrite.dstBinding = p_->passData[pass].uniformTextureBindings[name];
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(p_->backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void HawkEye::Pipeline::SetStorage(const std::string& name, HBuffer storageBuffer, int pass)
{
	auto it = p_->passData[pass].storageBufferBindings.find(name);
	if (it == p_->passData[pass].storageBufferBindings.end())
	{
		CoreLogError(VulkanLogger, "Pipeline: Storage buffer binding with name %s does not exist.", name.c_str());
		return;
	}

	WaitForUpload((HawkEye::HRendererData)p_->backendData, storageBuffer);

	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = storageBuffer->buffer.buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = storageBuffer->dataSize;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = p_->passData[pass].descriptorData.descriptorSet;
	descriptorWrite.dstBinding = p_->passData[pass].storageBufferBindings[name];
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(p_->backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void HawkEye::Pipeline::SetUniformImpl(const std::string& name, void* data, int dataSize)
{
	// TODO: Doing this - having one uniform for all frames in flight - may result in e.g., screen tearing.
	auto it = p_->uniformBuffers.find(name);
	if (it == p_->uniformBuffers.end())
	{
		CoreLogError(VulkanLogger, "Pipeline: Uniform buffer with name %s does not exist.", name.c_str());
		return;
	}

	if (dataSize != p_->uniformBuffers[name]->dataSize)
	{
		CoreLogError(VulkanLogger, "Pipeline: Data size does not match uniform buffer size (%s).", name.c_str());
		return;
	}

	HawkEye::UpdateBuffer((HawkEye::HRendererData)p_->backendData, p_->uniformBuffers[name], data, dataSize);
}

void HawkEye::Pipeline::SetUniformImpl(const std::string& name, void* data, int dataSize, int pass)
{
	if (p_->passData.size() <= pass)
	{
		CoreLogError(VulkanLogger, "Pipeline: Pass not present.");
		return;
	}

	auto it = p_->passData[pass].uniformBuffers.find(name);
	if (it == p_->passData[pass].uniformBuffers.end())
	{
		CoreLogError(VulkanLogger, "Pipeline: Uniform buffer with name %s does not exist.", name.c_str());
		return;
	}

	if (dataSize != p_->passData[pass].uniformBuffers[name]->dataSize)
	{
		CoreLogError(VulkanLogger, "Pipeline: Data size does not match uniform buffer size (%s).", name.c_str());
		return;
	}

	HawkEye::UpdateBuffer((HawkEye::HRendererData)p_->backendData, p_->passData[pass].uniformBuffers[name], data, dataSize);
}

HawkEye::HMaterial HawkEye::Pipeline::CreateMaterialImpl(void* data, int dataSize, int pass)
{
	if (p_->passData.size() <= pass)
	{
		CoreLogError(VulkanLogger, "Pipeline: Pass not present.");
		return -1;
	}

	if (p_->passData[pass].type == PipelinePass::Type::Computed)
	{
		CoreLogError(VulkanLogger, "Pipeline: Invalid pass for material creation.");
		return -1;
	}

	if (dataSize != p_->passData[pass].materialSize)
	{
		CoreLogError(VulkanLogger, "Pipeline: Material size does not match with configuration.");
		return -1;
	}

	const VulkanBackend::BackendData& backendData = *p_->backendData;

	int cumulativeSize = 0;
	int firstIndex = (int)p_->passData[pass].materialBuffers.size();

	std::string prepend = "_" + std::to_string(firstIndex) + "-";

	auto poolSizes = DescriptorUtils::GetPoolSizes((HRendererData)p_->backendData, p_->passData[pass].materialData, BufferType::DeviceLocal,
		p_->passData[pass].materialBuffers, prepend, data);

	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
	// TODO: Storage buffer bindings.
	DescriptorUtils::UpdateSets((HRendererData)p_->backendData, backendData, p_->passData[pass].materialData, poolSizes,
		descriptorPool, descriptorSet, p_->passData[pass].descriptorSetLayouts[0], p_->passData[pass].materialBuffers,
		/*not used inside*/p_->passData[pass].uniformTextureBindings, p_->passData[pass].storageBufferBindings, prepend, data);

	p_->passData[pass].materials.push_back({ descriptorPool, descriptorSet });

	return (HawkEye::HMaterial)(p_->passData[pass].materials.size() - 1);
}

VkFormat PipelineUtils::GetAttributeFormat(const PipelinePass::VertexAttribute& vertexAttribute)
{
	return (VkFormat)(VK_FORMAT_R32_UINT + (vertexAttribute.byteCount / 4 - 1) * 3 + (int)vertexAttribute.type);
}
