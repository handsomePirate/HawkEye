#include "HawkEye/HawkEyeAPI.hpp"
#include "YAMLConfiguration.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <VulkanBackend/ErrorCheck.hpp>
#define CompilerLogger VulkanLogger
#include <VulkanShaderCompiler/Logger.hpp>
#include <VulkanShaderCompiler/VulkanShaderCompilerAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>
#include <memory>

struct HawkEye::Pipeline::Private
{
	// TODO: Currently only one layer supported.
	std::vector<PipelineLayer> layers;
	VulkanBackend::BackendData* backendData = nullptr;
	std::unique_ptr<VulkanBackend::SurfaceData> surfaceData = nullptr;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> framebuffers;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	std::vector<VkShaderModule> shaderModules;
	VkPipeline computePipeline = VK_NULL_HANDLE;
	VkPipeline rasterizationPipeline = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue computeQueue = VK_NULL_HANDLE;
	VkSemaphore graphicsSemaphore = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	std::vector<VkFence> frameFences;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	uint32_t currentFrame = 0;
};

HawkEye::Pipeline::Pipeline()
	: p_(new Private) {}

HawkEye::Pipeline::~Pipeline()
{
	delete p_;
}

void HawkEye::Pipeline::Configure(RendererData rendererData, const char* configFile, int width, int height,
	void* windowHandle, void* windowConnection)
{
	const VulkanBackend::BackendData& backendData = *(VulkanBackend::BackendData*)rendererData;
	
	YAML::Node configData = YAML::LoadFile(configFile);

	auto& layers = p_->layers;
	if (configData["Layers"])
	{
		if (!configData["Layers"].IsSequence())
		{
			CoreLogError(VulkanLogger, "Pipeline: Wrong format for layers.");
			return;
		}

		for (int l = 0; l < configData["Layers"].size(); ++l)
		{
			layers.push_back(ConfigureLayer(configData["Layers"][l]));
		}
	}
	else
	{
		CoreLogWarn(VulkanLogger, "Pipeline: Configuration missing layers.");
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

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	VkPipelineLayout pipelineLayout;
	VulkanCheck(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	p_->pipelineLayout = pipelineLayout;

	if (layers[0].type == PipelineLayer::Type::Rasterized)
	{
		// TODO: Sample count.
		std::vector<VkDynamicState> dynamiceStates =
		{
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_VIEWPORT
		};
		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages(2);
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].pName = "main";
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].pName = "main";

		for (int s = 0; s < layers[0].shaders.size(); ++s)
		{
			shaderStages[0].module = VulkanShaderCompiler::Compile(device, layers[0].shaders[0].second.c_str());
			p_->shaderModules.push_back(shaderStages[0].module);

			shaderStages[1].module = VulkanShaderCompiler::Compile(device, layers[0].shaders[1].second.c_str());
			p_->shaderModules.push_back(shaderStages[1].module);
		}

		p_->rasterizationPipeline = VulkanBackend::CreateGraphicsPipeline(backendData, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
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

	p_->commandPool = VulkanBackend::CreateCommandPool(backendData, backendData.generalFamilyIndex);
	p_->commandBuffers.resize(p_->swapchainImageViews.size());
	VulkanBackend::AllocateCommandBuffers(backendData, p_->commandPool, p_->commandBuffers.data(), (uint32_t)p_->swapchainImageViews.size());

	// Record command buffer.
	for (int c = 0; c < p_->commandBuffers.size(); ++c)
	{
		VkCommandBufferBeginInfo commandBufferBeginInfo{};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		VulkanCheck(vkBeginCommandBuffer(p_->commandBuffers[c], &commandBufferBeginInfo));

		VkClearValue clearValues[1];
		clearValues[0].color = { 0.f, 0.f, 0.f };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = p_->framebuffers[c];
		vkCmdBeginRenderPass(p_->commandBuffers[c], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = (float)width;
		viewport.height = (float)height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		vkCmdSetViewport(p_->commandBuffers[c], 0, 1, &viewport);

		VkRect2D scissor;
		scissor.offset = { 0, 0 };
		scissor.extent = VkExtent2D{ (uint32_t)width, (uint32_t)height };
		vkCmdSetScissor(p_->commandBuffers[c], 0, 1, &scissor);

		// Display ray traced image generated by compute shader as a full screen quad
		// Quad vertices are generated in the vertex shader
		//vkCmdBindDescriptorSets(p_->commandBuffers[c], VK_PIPELINE_BIND_POINT_GRAPHICS, p_->pipelineLayout,
		//	0, 1, &data.DescriptorSet, 0, NULL);
		vkCmdBindPipeline(p_->commandBuffers[c], VK_PIPELINE_BIND_POINT_GRAPHICS, p_->rasterizationPipeline);
		vkCmdDraw(p_->commandBuffers[c], 3, 1, 0, 0);

		vkCmdEndRenderPass(p_->commandBuffers[c]);

		VulkanCheck(vkEndCommandBuffer(p_->commandBuffers[c]));
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

		for (int c = 0; c < p_->commandBuffers.size(); ++c)
		{
			VulkanBackend::FreeCommandBuffer(backendData, p_->commandPool, p_->commandBuffers[c]);
		}
		if (p_->commandPool)
		{
			VulkanBackend::DestroyCommandPool(backendData, p_->commandPool);
		}
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

	vkWaitForFences(device, 1, &p_->frameFences[p_->currentFrame], VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &p_->frameFences[p_->currentFrame]);

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
	
	static VkPipelineStageFlags pipelineStageWait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.commandBufferCount = 1;
	submitInfo.pWaitDstStageMask = &pipelineStageWait;
	submitInfo.pWaitSemaphores = &p_->presentSemaphore;
	submitInfo.pSignalSemaphores = &p_->graphicsSemaphore;
	submitInfo.pCommandBuffers = &p_->commandBuffers[currentImageIndex];

	VulkanCheck(vkQueueSubmit(p_->graphicsQueue, 1, &submitInfo, p_->frameFences[p_->currentFrame]));

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
	VulkanCheck(vkQueueWaitIdle(p_->surfaceData->defaultPresentQueue));

	p_->currentFrame = (p_->currentFrame + 1) % (uint32_t)p_->frameFences.size();
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
		VkFramebuffer framebuffer = VulkanBackend::CreateFramebuffer(backendData, width, height, p_->renderPass, { p_->swapchainImageViews[f] });
		p_->framebuffers[f] = framebuffer;
	}

	vkResetCommandPool(device, p_->commandPool, 0);

	for (int c = 0; c < p_->commandBuffers.size(); ++c)
	{
		VkCommandBufferBeginInfo commandBufferBeginInfo{};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		VulkanCheck(vkBeginCommandBuffer(p_->commandBuffers[c], &commandBufferBeginInfo));

		VkClearValue clearValues[1];
		clearValues[0].color = { 0.f, 0.f, 0.f };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = p_->renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = p_->framebuffers[c];
		vkCmdBeginRenderPass(p_->commandBuffers[c], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// HACK: Without dynamic viewports and other states, the pipeline might need to be recreated.
		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = (float)width;
		viewport.height = (float)height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		vkCmdSetViewport(p_->commandBuffers[c], 0, 1, &viewport);

		VkRect2D scissor;
		scissor.offset = { 0, 0 };
		scissor.extent = VkExtent2D{ (uint32_t)width, (uint32_t)height };
		vkCmdSetScissor(p_->commandBuffers[c], 0, 1, &scissor);

		// Display ray traced image generated by compute shader as a full screen quad
		// Quad vertices are generated in the vertex shader
		//vkCmdBindDescriptorSets(p_->commandBuffers[c], VK_PIPELINE_BIND_POINT_GRAPHICS, p_->pipelineLayout,
		//	0, 1, &data.DescriptorSet, 0, NULL);
		vkCmdBindPipeline(p_->commandBuffers[c], VK_PIPELINE_BIND_POINT_GRAPHICS, p_->rasterizationPipeline);
		vkCmdDraw(p_->commandBuffers[c], 3, 1, 0, 0);

		vkCmdEndRenderPass(p_->commandBuffers[c]);

		VulkanCheck(vkEndCommandBuffer(p_->commandBuffers[c]));
	}
}
