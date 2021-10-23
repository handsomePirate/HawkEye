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
	VkFence renderingFence = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
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
	VulkanBackend::BackendData* backendData = (VulkanBackend::BackendData*)rendererData;
	
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

	p_->backendData = backendData;

	p_->surfaceData = std::make_unique<VulkanBackend::SurfaceData>();
	VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
	surfaceData.width = width;
	surfaceData.height = height;
	VkInstance instance = backendData->instance;
	VkPhysicalDevice physicalDevice = backendData->physicalDevice;
	VkDevice device = backendData->logicalDevice;

	if (windowHandle)
	{
		VulkanBackend::CreateSurface(instance, surfaceData, windowHandle, windowConnection);
		VulkanBackend::GetDepthFormat(physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceFormat(physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceCapabilities(physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceExtent(physicalDevice, surfaceData);
		VulkanBackend::GetPresentMode(physicalDevice, surfaceData);
		VulkanBackend::GetSwapchainImageCount(surfaceData);

		VulkanBackend::FilterPresentQueues(*backendData, surfaceData);

		VulkanBackend::SelectPresentQueue(*backendData, surfaceData);
		VulkanBackend::SelectPresentComputeQueue(*backendData, surfaceData);

		p_->swapchain = VulkanBackend::CreateSwapchain(*backendData, surfaceData);

		std::vector<VkImage> swapchainImages;
		VulkanBackend::GetSwapchainImages(device, p_->swapchain, swapchainImages);

		p_->swapchainImageViews.resize(swapchainImages.size());
		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			p_->swapchainImageViews[i] = VulkanBackend::CreateImageView2D(device, swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	VkRenderPass renderPass = VulkanBackend::CreateRenderPass(device, surfaceData);
	p_->renderPass = renderPass;

	for (int v = 0; v < p_->swapchainImageViews.size(); ++v)
	{
		VkFramebuffer framebuffer = VulkanBackend::CreateFramebuffer(device, width, height, renderPass, { p_->swapchainImageViews[v] });
		p_->framebuffers.push_back(framebuffer);
	}

	VkPipelineCache pipelineCache = VulkanBackend::CreatePipelineCache(device);
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

		p_->rasterizationPipeline = VulkanBackend::CreateGraphicsPipeline(device, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
			VK_SAMPLE_COUNT_1_BIT, dynamiceStates, vertexInput, renderPass, pipelineLayout, shaderStages, pipelineCache);
	}
	else
	{
		// TODO: Compute version.
	}

	p_->graphicsQueue = backendData->generalQueues[0];

	p_->graphicsSemaphore = VulkanBackend::CreateSemaphore(device);
	p_->presentSemaphore = VulkanBackend::CreateSemaphore(device);
	p_->renderingFence = VulkanBackend::CreateFence(device);

	p_->commandPool = VulkanBackend::CreateCommandPool(device, backendData->generalFamilyIndex);
	p_->commandBuffers.resize(p_->swapchainImageViews.size());
	VulkanBackend::AllocateCommandBuffers(device, p_->commandPool, p_->commandBuffers.data(), (uint32_t)p_->swapchainImageViews.size());

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
		VkDevice device = p_->backendData->logicalDevice;
		vkDeviceWaitIdle(device);

		if (p_->rasterizationPipeline)
		{
			VulkanBackend::DestroyPipeline(device, p_->rasterizationPipeline);
		}
		if (p_->computePipeline)
		{
			VulkanBackend::DestroyPipeline(device, p_->computePipeline);
		}

		for (int s = 0; s < p_->shaderModules.size(); ++s)
		{
			VulkanBackend::DestroyShaderModule(device, p_->shaderModules[s]);
		}
		p_->shaderModules.clear();

		if (p_->pipelineLayout)
		{
			VulkanBackend::DestroyPipelineLayout(device, p_->pipelineLayout);
		}
		if (p_->pipelineCache)
		{
			VulkanBackend::DestroyPipelineCache(device, p_->pipelineCache);
		}

		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			VulkanBackend::DestroyFramebuffer(device, p_->framebuffers[i]);
		}
		p_->framebuffers.clear();
		VulkanBackend::DestroyRenderPass(device, p_->renderPass);
		if (p_->swapchain)
		{
			for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
			{
				VulkanBackend::DestroyImageView(device, p_->swapchainImageViews[i]);
			}
			p_->swapchainImageViews.clear();
			
			VulkanBackend::DestroySwapchain(device, p_->swapchain);
		}
		if (p_->surfaceData->surface)
		{
			VulkanBackend::DestroySurface(p_->backendData->instance, p_->surfaceData->surface);
		}

		if (p_->graphicsSemaphore)
		{
			VulkanBackend::DestroySemaphore(device, p_->graphicsSemaphore);
		}
		if (p_->presentSemaphore)
		{
			VulkanBackend::DestroySemaphore(device, p_->presentSemaphore);
		}
		if (p_->renderingFence)
		{
			VulkanBackend::DestroyFence(device, p_->renderingFence);
		}

		for (int c = 0; c < p_->commandBuffers.size(); ++c)
		{
			VulkanBackend::FreeCommandBuffer(device, p_->commandPool, p_->commandBuffers[c]);
		}
		if (p_->commandPool)
		{
			VulkanBackend::DestroyCommandPool(device, p_->commandPool);
		}
	}
}

void HawkEye::Pipeline::DrawFrame()
{
	VulkanBackend::BackendData* backendData = p_->backendData;

	uint32_t currentImageIndex;
	VkResult result = vkAcquireNextImageKHR(backendData->logicalDevice, p_->swapchain, UINT64_MAX, p_->presentSemaphore,
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

	VulkanCheck(vkQueueSubmit(p_->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));

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
}

void HawkEye::Pipeline::Resize(int width, int height)
{
	if (!p_->backendData)
	{
		return;
	}

	VkPhysicalDevice physicalDevice = p_->backendData->physicalDevice;
	VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
	VkDevice device = p_->backendData->logicalDevice;

	surfaceData.width = width;
	surfaceData.height = height;

	vkDeviceWaitIdle(device);

	for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
	{
		VulkanBackend::DestroyFramebuffer(device, p_->framebuffers[i]);
	}

	for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
	{
		VulkanBackend::DestroyImageView(device, p_->swapchainImageViews[i]);
	}

	if (p_->surfaceData->surface)
	{
		VulkanBackend::GetSurfaceCapabilities(physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceExtent(physicalDevice, surfaceData);

		p_->swapchain = VulkanBackend::RecreateSwapchain(*p_->backendData, surfaceData, p_->swapchain);

		std::vector<VkImage> swapchainImages;
		VulkanBackend::GetSwapchainImages(device, p_->swapchain, swapchainImages);

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->swapchainImageViews.size(); ++i)
		{
			p_->swapchainImageViews[i] = VulkanBackend::CreateImageView2D(device, swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	for (int f = 0; f < p_->framebuffers.size(); ++f)
	{
		VkFramebuffer framebuffer = VulkanBackend::CreateFramebuffer(device, width, height, p_->renderPass, { p_->swapchainImageViews[f] });
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
