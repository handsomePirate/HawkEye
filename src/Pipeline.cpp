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

void HawkEye::Pipeline::Configure(HRendererData rendererData, const char* configFile, int width, int height,
	void* windowHandle, void* windowConnection)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;
	
	YAML::Node configData = YAML::LoadFile(configFile);

	auto& passes = p_->passes;
	if (configData["Passes"])
	{
		if (!configData["Passes"].IsSequence())
		{
			CoreLogError(VulkanLogger, "Pipeline: Wrong format for passes.");
			return;
		}

		for (int l = 0; l < configData["Passes"].size(); ++l)
		{
			passes.push_back(ConfigureLayer(configData["Passes"][l]));
		}
	}
	else
	{
		CoreLogWarn(VulkanLogger, "Pipeline: Configuration missing passes.");
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

	if (!passes[0].material.empty())
	{
		for (int u = 0; u < passes[0].material.size(); ++u)
		{
			p_->materialSize += passes[0].material[u].size;
		}
		p_->materialData = passes[0].material;

		std::vector<VkDescriptorSetLayoutBinding> layoutBindings(p_->materialData.size());
		for (int b = 0; b < layoutBindings.size(); ++b)
		{
			layoutBindings[b].binding = b;
			layoutBindings[b].descriptorCount = 1;
			layoutBindings[b].descriptorType = p_->materialData[b].type;
			layoutBindings[b].stageFlags = p_->materialData[b].visibility;
		}

		p_->descriptorSetLayouts.push_back(VulkanBackend::CreateDescriptorSetLayout(backendData, layoutBindings));
	}

	if (!passes[0].uniforms.empty())
	{
		std::vector<VkDescriptorSetLayoutBinding> layoutBindings(passes[0].uniforms.size());
		for (int b = 0; b < layoutBindings.size(); ++b)
		{
			layoutBindings[b].binding = b;
			layoutBindings[b].descriptorCount = 1;
			layoutBindings[b].descriptorType = passes[0].uniforms[b].type;
			layoutBindings[b].stageFlags = passes[0].uniforms[b].visibility;
		}

		p_->descriptorSetLayouts.push_back(VulkanBackend::CreateDescriptorSetLayout(backendData, layoutBindings));

		std::vector<VkDescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		for (int u = 0; u < passes[0].uniforms.size(); ++u)
		{
			if (passes[0].uniforms[u].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				p_->uniformBuffers[passes[0].uniforms[u].name] = HawkEye::UploadBuffer(rendererData, nullptr,
					passes[0].uniforms[u].size, HawkEye::BufferUsage::Uniform, HawkEye::BufferType::Mapped);
				++poolSizes[0].descriptorCount;
			}
			else if (passes[0].uniforms[u].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				++poolSizes[1].descriptorCount;
			}
		}

		std::vector<VkDescriptorPoolSize> filteredPoolSizes;
		for (int s = 0; s < poolSizes.size(); ++s)
		{
			if (poolSizes[s].descriptorCount > 0)
			{
				filteredPoolSizes.push_back(poolSizes[s]);
			}
		}
		
		poolSizes[0].descriptorCount = (uint32_t)passes[0].uniforms.size();

		for (int f = 0; f < p_->frameData.size(); ++f)
		{
			p_->frameData[f].descriptorPool = VulkanBackend::CreateDescriptorPool(backendData, filteredPoolSizes, 1);
			p_->frameData[f].descriptorSet = VulkanBackend::AllocateDescriptorSet(backendData,
				p_->frameData[f].descriptorPool, p_->descriptorSetLayouts[1]);
		}

		for (int f = 0; f < p_->frameData.size(); ++f)
		{
			int k = 0;
			while (k < passes[0].uniforms.size())
			{
				if (passes[0].uniforms[k].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				{
					p_->uniformTextureBindings[passes[0].uniforms[k].name] = k;
					++k;
					continue;
				}

				std::vector<VkDescriptorBufferInfo> bufferInfos;

				int u = k;
				for (; u < passes[0].uniforms.size() &&
					passes[0].uniforms[u].visibility == passes[0].uniforms[k].visibility &&
					passes[0].uniforms[u].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					++u)
				{
					VkDescriptorBufferInfo bufferInfo{};
					bufferInfo.buffer = p_->uniformBuffers[passes[0].uniforms[u].name]->buffer.buffer;
					bufferInfo.offset = 0;
					bufferInfo.range = (VkDeviceSize)passes[0].uniforms[u].size;

					bufferInfos.push_back(bufferInfo);
				}

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = p_->frameData[f].descriptorSet;
				descriptorWrite.dstBinding = k;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = passes[0].uniforms[k].type;
				descriptorWrite.descriptorCount = (uint32_t)bufferInfos.size();
				descriptorWrite.pBufferInfo = bufferInfos.data();

				vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
				k += (int32_t)bufferInfos.size();
			}
		}
	}

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)p_->descriptorSetLayouts.size();
	pipelineLayoutCreateInfo.pSetLayouts = p_->descriptorSetLayouts.data();
	VkPipelineLayout pipelineLayout;
	VulkanCheck(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	p_->pipelineLayout = pipelineLayout;

	if (passes[0].type == PipelinePass::Type::Rasterized)
	{
		// TODO: Sample count.
		std::vector<VkDynamicState> dynamiceStates =
		{
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_VIEWPORT
		};

		std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions(passes[0].attributes.size());
		int vertexSize = 0;
		for (int a = 0; a < passes[0].attributes.size(); ++a)
		{
			vertexAttributeDescriptions[a].binding = 0;
			// TODO: Make format conform to what the input was.
			vertexAttributeDescriptions[a].format = GetAttributeFormat(passes[0].attributes[a]);
			vertexAttributeDescriptions[a].location = a;
			vertexAttributeDescriptions[a].offset = vertexSize;
			vertexSize += passes[0].attributes[a].byteCount;
		}
		p_->vertexSize = vertexSize;

		VkVertexInputBindingDescription vertexBindingDescription{};
		vertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		vertexBindingDescription.stride = vertexSize;
		vertexBindingDescription.binding = 0;

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		// TODO: Split attributes into multiple buffers.
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &vertexBindingDescription;
		vertexInput.vertexAttributeDescriptionCount = (uint32_t)vertexAttributeDescriptions.size();
		vertexInput.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages(2);
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].pName = "main";
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].pName = "main";

		for (int s = 0; s < passes[0].shaders.size(); ++s)
		{
			shaderStages[s].module = VulkanShaderCompiler::Compile(device, passes[0].shaders[s].second.c_str());
			p_->shaderModules.push_back(shaderStages[s].module);
		}

		p_->rasterizationPipeline = VulkanBackend::CreateGraphicsPipeline(backendData,
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

	CoreLogDebug(VulkanLogger, "Pipeline: Configuration successful.");
}

void HawkEye::Pipeline::Shutdown()
{
	if (p_->backendData)
	{
		const VulkanBackend::BackendData& backendData = *p_->backendData;
		VkDevice device = backendData.logicalDevice;
		vkDeviceWaitIdle(device);

		if (p_->rasterizationPipeline)
		{
			VulkanBackend::DestroyPipeline(backendData, p_->rasterizationPipeline);
		}
		if (p_->computePipeline)
		{
			VulkanBackend::DestroyPipeline(backendData, p_->computePipeline);
		}

		for (int s = 0; s < p_->shaderModules.size(); ++s)
		{
			VulkanBackend::DestroyShaderModule(backendData, p_->shaderModules[s]);
		}
		p_->shaderModules.clear();

		if (p_->pipelineLayout)
		{
			VulkanBackend::DestroyPipelineLayout(backendData, p_->pipelineLayout);
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

		for (int s = 0; s < p_->descriptorSetLayouts.size(); ++s)
		{
			VulkanBackend::DestroyDescriptorSetLayout(backendData, p_->descriptorSetLayouts[s]);
		}
		for (int m = 0; m < p_->materials.size(); ++m)
		{
			VulkanBackend::DestroyDescriptorPool(backendData, p_->materials[m].descriptorPool);
		}
		for (int b = 0; b < p_->materialBuffers.size(); ++b)
		{
			HawkEye::DeleteBuffer((HawkEye::HRendererData)p_->backendData, p_->materialBuffers[b]);
		}
		for (int p = 0; p < p_->frameData.size(); ++p)
		{
			VulkanBackend::DestroyDescriptorPool(backendData, p_->frameData[p].descriptorPool);
		}
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

void HawkEye::Pipeline::UseBuffers(DrawBuffer* drawBuffers, int bufferCount)
{
	p_->drawBuffers.clear();
	if (drawBuffers && bufferCount > 0)
	{
		for (int b = 0; b < bufferCount; ++b)
		{
			if (drawBuffers[b].material >= p_->materials.size() || drawBuffers[b].material < 0)
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

			p_->drawBuffers[drawBuffers[b].material].push_back(drawBuffers[b]);
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
	UseBuffers(nullptr, 0);

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

	for (int f = 0; f < p_->frameData.size(); ++f)
	{
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = texture->imageLayout;
		imageInfo.imageView = texture->imageView;
		imageInfo.sampler = texture->sampler;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = p_->frameData[f].descriptorSet;
		descriptorWrite.dstBinding = p_->uniformTextureBindings[name];
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(p_->backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
	}
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

HawkEye::HMaterial HawkEye::Pipeline::CreateMaterialImpl(void* data, int dataSize)
{
	if (dataSize != p_->materialSize)
	{
		CoreLogError(VulkanLogger, "Pipeline: Material size does not match with configuration.");
		return -1;
	}

	const VulkanBackend::BackendData& backendData = *p_->backendData;

	int cumulativeSize = 0;
	int firstIndex = p_->materialBuffers.size();
	std::vector<VkDescriptorPoolSize> poolSizes(2);
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	for (int u = 0; u < p_->materialData.size(); ++u)
	{
		if (p_->materialData[u].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		{
			p_->materialBuffers.push_back(HawkEye::UploadBuffer((HRendererData)p_->backendData,
				(void*)((int*)data + (cumulativeSize >> 2)), p_->materialData[u].size,
				HawkEye::BufferUsage::Uniform, HawkEye::BufferType::DeviceLocal));
			++poolSizes[0].descriptorCount;
		}
		else if (p_->materialData[u].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			++poolSizes[1].descriptorCount;
		}
		cumulativeSize += p_->materialData[u].size;
	}
	std::vector<VkDescriptorPoolSize> filteredPoolSizes;
	for (int s = 0; s < poolSizes.size(); ++s)
	{
		if (poolSizes[s].descriptorCount > 0)
		{
			filteredPoolSizes.push_back(poolSizes[s]);
		}
	}

	VkDescriptorPool descriptorPool = VulkanBackend::CreateDescriptorPool(backendData, filteredPoolSizes, 1);
	VkDescriptorSet descriptorSet = VulkanBackend::AllocateDescriptorSet(backendData, descriptorPool,
		p_->descriptorSetLayouts[0]);
	p_->materials.push_back({ descriptorPool, descriptorSet });

	int k = 0;
	cumulativeSize = 0;
	while (k < p_->materialData.size())
	{
		if (p_->materialData[k].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			HTexture texture = *(HTexture*)((int*)data + (cumulativeSize >> 2));
			WaitForUpload((HawkEye::HRendererData)p_->backendData, texture);

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

			vkUpdateDescriptorSets(p_->backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
			cumulativeSize += p_->materialData[k].size;
			++k;
		}
		else
		{
			std::vector<VkDescriptorBufferInfo> bufferInfos;

			int u = k;
			for (; u < p_->materialData.size() &&
				p_->materialData[u].visibility == p_->materialData[k].visibility &&
				p_->materialData[u].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				++u)
			{
				HBuffer buffer = p_->materialBuffers[firstIndex + u];
				WaitForUpload((HawkEye::HRendererData)p_->backendData, buffer);
				buffer->firstUse = false;
				VkDescriptorBufferInfo bufferInfo{};
				bufferInfo.buffer = buffer->buffer.buffer;
				bufferInfo.offset = 0;
				bufferInfo.range = (VkDeviceSize)p_->materialData[u].size;

				bufferInfos.push_back(bufferInfo);
				cumulativeSize += p_->materialData[u].size;
			}

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSet;
			descriptorWrite.dstBinding = k;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = p_->materialData[k].type;
			descriptorWrite.descriptorCount = (uint32_t)bufferInfos.size();
			descriptorWrite.pBufferInfo = bufferInfos.data();

			vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
			k += (int32_t)bufferInfos.size();
		}
	}

	return p_->materials.size() - 1;
}
