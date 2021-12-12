#include "Pipeline.hpp"
#include "Resources.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/ErrorCheck.hpp>
#define CompilerLogger VulkanLogger
#include <VulkanShaderCompiler/Logger.hpp>
#include <VulkanShaderCompiler/VulkanShaderCompilerAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>
#include <memory>

HawkEye::Pipeline::Pipeline()
	: p_(new Private) {}

HawkEye::Pipeline::~Pipeline()
{
	delete p_;
}

VkFormat GetAttributeFormat(const PipelinePass::VertexAttribute& vertexAttribute)
{
	return (VkFormat)(VK_FORMAT_R32_UINT + (vertexAttribute.byteCount / 4 - 1) * 3 + (int)vertexAttribute.type);
}

bool HawkEye::Pipeline::Configured() const
{
	return p_->backendData != nullptr;
}

VkDescriptorSetLayout GetDescriptorSetLayout(const VulkanBackend::BackendData& backendData, const std::vector<UniformData>& uniformData)
{
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings(uniformData.size());
	for (int b = 0; b < layoutBindings.size(); ++b)
	{
		layoutBindings[b].binding = b;
		layoutBindings[b].descriptorCount = 1;
		layoutBindings[b].descriptorType = uniformData[b].type;
		layoutBindings[b].stageFlags = uniformData[b].visibility;
	}

	return VulkanBackend::CreateDescriptorSetLayout(backendData, layoutBindings);
}

std::vector<VkDescriptorPoolSize> FilterDescriptorPoolSizes(const std::vector<VkDescriptorPoolSize>& inPoolSizes)
{
	std::vector<VkDescriptorPoolSize> filteredPoolSizes;
	for (int s = 0; s < inPoolSizes.size(); ++s)
	{
		if (inPoolSizes[s].descriptorCount > 0)
		{
			filteredPoolSizes.push_back(inPoolSizes[s]);
		}
	}

	return filteredPoolSizes;
}

std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizes(HawkEye::HRendererData rendererData, const std::vector<UniformData>& uniformData,
	HawkEye::BufferType uniformBufferType, std::map<std::string, HawkEye::HBuffer>& uniformBuffers, void* data = nullptr)
{
	std::vector<VkDescriptorPoolSize> poolSizes(2);
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	int cumulativeSize = 0;
	for (int u = 0; u < uniformData.size(); ++u)
	{
		if (uniformData[u].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		{
			uniformBuffers[uniformData[u].name] = HawkEye::UploadBuffer(rendererData,
				data == nullptr ? data : (void*)((int*)data + (cumulativeSize >> 2)),
				uniformData[u].size, HawkEye::BufferUsage::Uniform, uniformBufferType);
			++poolSizes[0].descriptorCount;
		}
		else if (uniformData[u].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			++poolSizes[1].descriptorCount;
		}
		cumulativeSize += uniformData[u].size;
	}

	return FilterDescriptorPoolSizes(poolSizes);
}

void UpdateDescriptorSets(HawkEye::HRendererData rendererData, const VulkanBackend::BackendData& backendData,
	const std::vector<UniformData>& uniformData, const std::vector<VkDescriptorPoolSize>& poolSizes,
	VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet, VkDescriptorSetLayout descriptorSetLayout,
	const std::map<std::string, HawkEye::HBuffer>& uniformBuffers, std::map<std::string, int>& uniformTextureBindings,
	void* data = nullptr)
{
	descriptorPool = VulkanBackend::CreateDescriptorPool(backendData, poolSizes, 1);
	descriptorSet = VulkanBackend::AllocateDescriptorSet(backendData, descriptorPool, descriptorSetLayout);

	int k = 0;
	int cumulativeSize = 0;
	while (k < uniformData.size())
	{
		if (uniformData[k].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			if (!data)
			{
				uniformTextureBindings[uniformData[k].name] = k;
			}
			else
			{
				HawkEye::HTexture texture = *(HawkEye::HTexture*)((int*)data + (cumulativeSize >> 2));
				HawkEye::WaitForUpload(rendererData, texture);

				VkDescriptorImageInfo imageInfo{};
				imageInfo.imageLayout = texture->imageLayout;
				imageInfo.imageView = texture->imageView;
				imageInfo.sampler = texture->sampler;

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = descriptorSet;
				descriptorWrite.dstBinding = k;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pImageInfo = &imageInfo;

				vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
				cumulativeSize += uniformData[k].size;
			}
			++k;
			continue;
		}

		std::vector<VkDescriptorBufferInfo> bufferInfos;

		int u = k;
		for (; u < uniformData.size() &&
			uniformData[u].visibility == uniformData[k].visibility &&
			uniformData[u].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			++u)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffers.at(uniformData[u].name)->buffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = (VkDeviceSize)uniformData[u].size;

			bufferInfos.push_back(bufferInfo);
			cumulativeSize += uniformData[u].size;
		}

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSet;
		descriptorWrite.dstBinding = k;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = uniformData[k].type;
		descriptorWrite.descriptorCount = (uint32_t)bufferInfos.size();
		descriptorWrite.pBufferInfo = bufferInfos.data();

		vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
		k += (int32_t)bufferInfos.size();
	}
}

void HawkEye::Pipeline::Configure(HRendererData rendererData, const char* configFile, int width, int height,
	void* windowHandle, void* windowConnection)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;

	YAML::Node configData = YAML::LoadFile(configFile);

	auto& passes = p_->passes;
	if (configData["Pipeline"])
	{
		if (configData["Pipeline"]["samples"])
		{
			p_->samples = configData["Pipeline"]["samples"].as<int>();
		}
		else
		{
			CoreLogWarn(VulkanLogger, "Pipeline: Sample count not defined, 1 assumed.");
			p_->samples = 1;
		}
		
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

		p_->swapchain = VulkanBackend::CreateSwapchain(backendData, surfaceData);

		std::vector<VkImage> swapchainImages;
		VulkanBackend::GetSwapchainImages(backendData, p_->swapchain, swapchainImages);

		p_->swapchainImageViews.resize(swapchainImages.size());
		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			p_->swapchainImageViews[i] = VulkanBackend::CreateImageView2D(backendData, swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	VkRenderPass renderPass = VulkanBackend::CreateRenderPass(backendData, surfaceData);
	p_->renderPass = renderPass;

	for (int v = 0; v < p_->swapchainImageViews.size(); ++v)
	{
		VkFramebuffer framebuffer = VulkanBackend::CreateFramebuffer(backendData, width, height, renderPass, { p_->swapchainImageViews[v] });
		p_->framebuffers.push_back(framebuffer);
	}

	VkPipelineCache pipelineCache = VulkanBackend::CreatePipelineCache(backendData);
	p_->pipelineCache = pipelineCache;

	p_->frameData.resize(p_->swapchainImageViews.size());

	p_->passData.resize(passes.size());
	std::vector<VkDescriptorSetLayout> passUniformLayouts(passes.size());
	for (int p = 0; p < passes.size(); ++p)
	{
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

			p_->passData[p].descriptorSetLayouts.push_back(GetDescriptorSetLayout(backendData, p_->passData[p].materialData));
		}
		if (!passes[p].uniforms.empty())
		{
			passUniformLayouts[p] = GetDescriptorSetLayout(backendData, passes[p].uniforms);

			auto poolSizes = GetDescriptorPoolSizes(rendererData, passes[p].uniforms, HawkEye::BufferType::Mapped, p_->passData[p].uniformBuffers);

			UpdateDescriptorSets(rendererData, backendData, passes[p].uniforms, poolSizes, p_->passData[p].descriptorData.descriptorPool,
				p_->passData[p].descriptorData.descriptorSet, passUniformLayouts[p], p_->passData[p].uniformBuffers, p_->passData[p].uniformTextureBindings);
		}
	}

	VkDescriptorSetLayout commonDescriptorSetLayout = VK_NULL_HANDLE;
	if (!p_->uniformInfo.uniforms.empty())
	{
		commonDescriptorSetLayout = GetDescriptorSetLayout(backendData, p_->uniformInfo.uniforms);

		auto poolSizes = GetDescriptorPoolSizes(rendererData, p_->uniformInfo.uniforms, HawkEye::BufferType::Mapped, p_->uniformBuffers);

		UpdateDescriptorSets(rendererData, backendData, p_->uniformInfo.uniforms, poolSizes, p_->descriptorData.descriptorPool,
			p_->descriptorData.descriptorSet, commonDescriptorSetLayout, p_->uniformBuffers, p_->uniformTextureBindings);
	}

	for (int p = 0; p < passes.size(); ++p)
	{
		std::vector<VkDescriptorSetLayout> passSetLayouts = p_->passData[p].descriptorSetLayouts;
		if (commonDescriptorSetLayout != VK_NULL_HANDLE)
		{
			passSetLayouts.push_back(commonDescriptorSetLayout);
		}
		for (int l = 0; l < passUniformLayouts.size(); ++l)
		{
			passSetLayouts.push_back(passUniformLayouts[l]);
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
			std::vector<VkDynamicState> dynamiceStates =
			{
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_VIEWPORT
			};

			std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions(passes[p].attributes.size() + 4);
			int vertexSize = 0;
			for (int a = 0; a < passes[p].attributes.size(); ++a)
			{
				vertexAttributeDescriptions[a].binding = 0;
				vertexAttributeDescriptions[a].format = GetAttributeFormat(passes[p].attributes[a]);
				vertexAttributeDescriptions[a].location = a;
				vertexAttributeDescriptions[a].offset = vertexSize;
				vertexSize += passes[p].attributes[a].byteCount;
			}
			p_->passData[p].vertexSize = vertexSize;
			for (int a = passes[p].attributes.size(); a < passes[p].attributes.size() + 4; ++a)
			{
				vertexAttributeDescriptions[a].binding = 1;
				vertexAttributeDescriptions[a].format = VK_FORMAT_R32G32B32A32_SFLOAT;
				vertexAttributeDescriptions[a].location = a;
				vertexAttributeDescriptions[a].offset = (a - passes[p].attributes.size()) * 16;
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
			// TODO: Split attributes into multiple buffers.
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
				VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
				VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
				VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
				VK_SAMPLE_COUNT_1_BIT, dynamiceStates, vertexInput, renderPass, pipelineLayout, shaderStages, pipelineCache);
		}
		else
		{
			// TODO: Compute version.
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
		RecordCommands(c, backendData, p_);
	}

	VulkanBackend::DestroyDescriptorSetLayout(backendData, commonDescriptorSetLayout);
	for (int l = 0; l < passUniformLayouts.size(); ++l)
	{
		VulkanBackend::DestroyDescriptorSetLayout(backendData, passUniformLayouts[l]);
	}

	CoreLogDebug(VulkanLogger, "Pipeline: Configuration successful.");
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
		}
		if (p_->pipelineCache)
		{
			VulkanBackend::DestroyPipelineCache(backendData, p_->pipelineCache);
		}

		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			VulkanBackend::DestroyFramebuffer(backendData, p_->framebuffers[i]);
		}
		p_->framebuffers.clear();
		VulkanBackend::DestroyRenderPass(backendData, p_->renderPass);
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
	if (pass >= p_->passes.size() || pass < 0)
	{
		CoreLogError(VulkanLogger, "Pipeline: Invalid pass.");
		return;
	}

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

	const VulkanBackend::BackendData& backendData = *p_->backendData;
	VkDevice device = backendData.logicalDevice;

	uint32_t frameIndex = (uint32_t)(p_->currentFrame % p_->frameFences.size());

	vkWaitForFences(device, 1, &p_->frameFences[frameIndex], VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &p_->frameFences[frameIndex]);

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
	
	if (p_->frameData[currentImageIndex].dirty)
	{
		VulkanBackend::ResetCommandBuffer(p_->frameData[currentImageIndex].commandBuffer);
		RecordCommands(currentImageIndex, backendData, p_);
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

	const VulkanBackend::BackendData& backendData = *p_->backendData;
	VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
	VkDevice device = p_->backendData->logicalDevice;

	surfaceData.width = width;
	surfaceData.height = height;

	vkDeviceWaitIdle(device);

	for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
	{
		VulkanBackend::DestroyFramebuffer(backendData, p_->framebuffers[i]);
	}

	for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
	{
		VulkanBackend::DestroyImageView(backendData, p_->swapchainImageViews[i]);
	}

	if (p_->surfaceData->surface)
	{
		VulkanBackend::GetSurfaceCapabilities(backendData, surfaceData);
		VulkanBackend::GetSurfaceExtent(backendData, surfaceData);

		p_->swapchain = VulkanBackend::RecreateSwapchain(*p_->backendData, surfaceData, p_->swapchain);

		std::vector<VkImage> swapchainImages;
		VulkanBackend::GetSwapchainImages(backendData, p_->swapchain, swapchainImages);

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			p_->swapchainImageViews[i] = VulkanBackend::CreateImageView2D(backendData, swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	for (int f = 0; f < p_->framebuffers.size(); ++f)
	{
		VkFramebuffer framebuffer = VulkanBackend::CreateFramebuffer(backendData, width, height, p_->renderPass,
			{ p_->swapchainImageViews[f] });
		p_->framebuffers[f] = framebuffer;
	}

	for (int c = 0; c < p_->frameData.size(); ++c)
	{
		p_->frameData[c].dirty = true;
	}
}

void HawkEye::Pipeline::ReleaseResources()
{
	// Stop using buffer resources.
	// TODO: Do for all passes.
	for (int p = 0; p < p_->passes.size(); ++p)
	{
		UseBuffers(nullptr, 0, p);
	}

	// Wait for frames in flight to finish using the resources.
	for (int f = 0; f < GetFramesInFlight(); ++f)
	{
		DrawFrame();
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
	descriptorWrite.dstSet = p_->passData[pass].descriptorData.descriptorSet;
	descriptorWrite.dstBinding = p_->passData[pass].uniformTextureBindings[name];
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(p_->backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void HawkEye::Pipeline::SetUniformImpl(const std::string& name, void* data, int dataSize)
{
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
	if (dataSize != p_->passData[pass].materialSize)
	{
		CoreLogError(VulkanLogger, "Pipeline: Material size does not match with configuration.");
		return -1;
	}

	const VulkanBackend::BackendData& backendData = *p_->backendData;

	int cumulativeSize = 0;
	int firstIndex = p_->passData[pass].materialBuffers.size();

	auto poolSizes = GetDescriptorPoolSizes((HRendererData)p_->backendData, p_->passData[pass].materialData, BufferType::DeviceLocal,
		p_->passData[pass].materialBuffers, data);

	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
	UpdateDescriptorSets((HRendererData)p_->backendData, backendData, p_->passData[pass].materialData, poolSizes,
		descriptorPool, descriptorSet, p_->passData[pass].descriptorSetLayouts[0], p_->passData[pass].materialBuffers,
		/*not used inside*/p_->passData[pass].uniformTextureBindings, data);

	p_->passData[pass].materials.push_back({ descriptorPool, descriptorSet });

	return p_->passData[pass].materials.size() - 1;
}
