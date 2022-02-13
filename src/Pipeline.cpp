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
	YAML::Node configData = YAML::LoadFile(configFile);

	p_->commonFrameData.backendData = (VulkanBackend::BackendData*)rendererData;
	const VulkanBackend::BackendData& backendData = *p_->commonFrameData.backendData;
	p_->commonFrameData.rendererData = rendererData;

	p_->commonFrameData.surfaceData = std::make_unique<VulkanBackend::SurfaceData>();
	VulkanBackend::SurfaceData& surfaceData = *p_->commonFrameData.surfaceData.get();
	surfaceData.width = width;
	surfaceData.height = height;

	if (windowHandle)
	{
		VulkanBackend::CreateSurface(backendData, surfaceData, windowHandle, windowConnection);
		VulkanBackend::GetDepthFormat(backendData, surfaceData);
		VulkanBackend::GetSurfaceFormat(backendData, surfaceData);
		VulkanBackend::GetSurfaceCapabilities(backendData, surfaceData);
		VulkanBackend::GetSurfaceExtent(backendData, surfaceData);
		VulkanBackend::GetPresentMode(backendData, surfaceData);
		VulkanBackend::GetSwapchainImageCount(surfaceData);
		p_->commonFrameData.framesInFlightCount = surfaceData.swapchainImageCount;

		VulkanBackend::FilterPresentQueues(backendData, surfaceData);

		VulkanBackend::SelectPresentQueue(backendData, surfaceData);
		VulkanBackend::SelectPresentComputeQueue(backendData, surfaceData);

		// TODO: Change based on frame graph requirements.
		p_->commonFrameData.swapchain = VulkanBackend::CreateSwapchain(backendData, surfaceData,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

		VulkanBackend::GetSwapchainImages(backendData, p_->commonFrameData.swapchain, p_->commonFrameData.swapchainImages);

		p_->commonFrameData.swapchainImageViews.resize(p_->commonFrameData.framesInFlightCount);
		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->commonFrameData.framesInFlightCount; ++i)
		{
			p_->commonFrameData.swapchainImageViews[i] = VulkanBackend::CreateImageView2D(backendData, p_->commonFrameData.swapchainImages[i],
				surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	p_->commonFrameData.targetSampler = VulkanBackend::CreateImageSampler(backendData, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
		VK_BORDER_COLOR_INT_TRANSPARENT_BLACK, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.f, 1);

	VkPipelineCache pipelineCache = VulkanBackend::CreatePipelineCache(backendData);
	p_->commonFrameData.pipelineCache = pipelineCache;

	p_->commonFrameData.graphicsQueue = backendData.generalQueues[0];

	p_->graphicsSemaphore = VulkanBackend::CreateSemaphore(backendData);
	p_->presentSemaphore = VulkanBackend::CreateSemaphore(backendData);
	for (int v = 0; v < p_->commonFrameData.framesInFlightCount; ++v)
	{
		p_->frameFences.push_back(VulkanBackend::CreateFence(backendData, VK_FENCE_CREATE_SIGNALED_BIT));
	}

	p_->commonFrameData.commandPool = VulkanBackend::CreateCommandPool(backendData, backendData.generalFamilyIndex,
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	std::vector<VkCommandBuffer> commandBuffers(p_->commonFrameData.framesInFlightCount);

	VulkanBackend::AllocateCommandBuffers(backendData, p_->commonFrameData.commandPool, commandBuffers.data(),
		(uint32_t)p_->commonFrameData.framesInFlightCount);

	p_->commonFrameData.commandBuffers.resize(p_->commonFrameData.framesInFlightCount);
	for (int c = 0; c < p_->commonFrameData.framesInFlightCount; ++c)
	{
		p_->commonFrameData.commandBuffers[c].commandBuffer = commandBuffers[c];
	}

	p_->frameGraph.Configure(configData["nodes"], p_->commonFrameData);

	p_->textureUpdateData.resize(p_->commonFrameData.framesInFlightCount);
	p_->bufferUpdateData.resize(p_->commonFrameData.framesInFlightCount);
	p_->preallocatedUpdateData.resize(p_->commonFrameData.framesInFlightCount);

	p_->configured = true;

	CoreLogInfo(VulkanLogger, "Pipeline: Configuration successful.");
}

void HawkEye::Pipeline::Shutdown()
{
	if (p_->configured)
	{
		const VulkanBackend::BackendData& backendData = *p_->commonFrameData.backendData;
		VkDevice device = backendData.logicalDevice;
		vkDeviceWaitIdle(device);

		// TODO: Detach the frame graph from the pipeline.
		p_->frameGraph.Shutdown(p_->commonFrameData);

		VulkanBackend::DestroyPipelineCache(backendData, p_->commonFrameData.pipelineCache);

		VulkanBackend::DestroyImageSampler(backendData, p_->commonFrameData.targetSampler);

		if (p_->commonFrameData.swapchain)
		{
			for (int i = 0; i < p_->commonFrameData.framesInFlightCount; ++i)
			{
				VulkanBackend::DestroyImageView(backendData, p_->commonFrameData.swapchainImageViews[i]);
			}
			p_->commonFrameData.swapchainImageViews.clear();

			VulkanBackend::DestroySwapchain(backendData, p_->commonFrameData.swapchain);
			VulkanBackend::DestroySurface(backendData, p_->commonFrameData.surfaceData->surface);
		}

		VulkanBackend::DestroySemaphore(backendData, p_->graphicsSemaphore);
		VulkanBackend::DestroySemaphore(backendData, p_->presentSemaphore);
		for (int f = 0; f < p_->frameFences.size(); ++f)
		{
			VulkanBackend::DestroyFence(backendData, p_->frameFences[f]);
		}

		for (int c = 0; c < p_->commonFrameData.framesInFlightCount; ++c)
		{
			VulkanBackend::FreeCommandBuffer(backendData, p_->commonFrameData.commandPool,
				p_->commonFrameData.commandBuffers[c].commandBuffer);
		}
		VulkanBackend::DestroyCommandPool(backendData, p_->commonFrameData.commandPool);
	}
}

void HawkEye::Pipeline::UseBuffers(const std::string& nodeName, DrawBuffer* drawBuffers, int bufferCount)
{
	p_->frameGraph.UseBuffers(nodeName, drawBuffers, bufferCount);

	for (int c = 0; c < p_->commonFrameData.framesInFlightCount; ++c)
	{
		p_->commonFrameData.commandBuffers[c].dirty = true;
	}
}

void HawkEye::Pipeline::DrawFrame()
{
	if (!p_->configured || p_->commonFrameData.surfaceData->width == 0 || p_->commonFrameData.surfaceData->height == 0)
	{
		return;
	}

	const VulkanBackend::BackendData& backendData = *p_->commonFrameData.backendData;
	VkDevice device = backendData.logicalDevice;

	uint32_t currentImageIndex = UINT32_MAX;
	VkResult result = vkAcquireNextImageKHR(device, p_->commonFrameData.swapchain, UINT64_MAX, p_->presentSemaphore,
		VK_NULL_HANDLE, &currentImageIndex);
	
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
	{
		CoreLogWarn(VulkanLogger, "Pipeline: Should resize.");
		return;
	}
	VulkanCheck(result);
	if (currentImageIndex == UINT32_MAX)
	{
		CoreLogFatal(VulkanLogger, "Error: Lost the swapchain.");
		throw std::runtime_error("Error: Lost the swapchain.");
	}

	vkWaitForFences(device, 1, &p_->frameFences[currentImageIndex], VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &p_->frameFences[currentImageIndex]);

	if (p_->commonFrameData.commandBuffers[currentImageIndex].dirty)
	{
		VulkanBackend::ResetCommandBuffer(p_->commonFrameData.commandBuffers[currentImageIndex].commandBuffer);
		p_->commonFrameData.commandBuffers[currentImageIndex].dirty = false;
		p_->frameGraph.Record(p_->commonFrameData.commandBuffers[currentImageIndex].commandBuffer, currentImageIndex, p_->commonFrameData);
	}

	auto millisecondsSinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	unsigned int concatTime = unsigned int(millisecondsSinceEpoch);
	
	UpdateUniforms(currentImageIndex);

	static VkPipelineStageFlags pipelineStageWait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.commandBufferCount = 1;
	submitInfo.pWaitDstStageMask = &pipelineStageWait;
	submitInfo.pWaitSemaphores = &p_->presentSemaphore;
	submitInfo.pSignalSemaphores = &p_->graphicsSemaphore;
	submitInfo.pCommandBuffers = &p_->commonFrameData.commandBuffers[currentImageIndex].commandBuffer;

	VulkanCheck(vkQueueSubmit(p_->commonFrameData.graphicsQueue, 1, &submitInfo, p_->frameFences[currentImageIndex]));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &p_->commonFrameData.swapchain;
	presentInfo.pImageIndices = &currentImageIndex;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &p_->graphicsSemaphore;
	result = vkQueuePresentKHR(p_->commonFrameData.surfaceData->defaultPresentQueue, &presentInfo);
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

	++p_->commonFrameData.currentFrame;
}

void HawkEye::Pipeline::Resize(int width, int height)
{
	if (!p_->configured || width == 0 || height == 0)
	{
		VulkanBackend::SurfaceData& surfaceData = *p_->commonFrameData.surfaceData.get();
		surfaceData.width = width;
		surfaceData.height = height;
		return;
	}

	assert(p_->configured);

	const VulkanBackend::BackendData& backendData = *p_->commonFrameData.backendData;
	VulkanBackend::SurfaceData& surfaceData = *p_->commonFrameData.surfaceData.get();
	VkDevice device = p_->commonFrameData.backendData->logicalDevice;

	vkDeviceWaitIdle(device);

	for (int i = 0; i < p_->commonFrameData.framesInFlightCount; ++i)
	{
		VulkanBackend::DestroyImageView(backendData, p_->commonFrameData.swapchainImageViews[i]);
	}

	if (p_->commonFrameData.surfaceData->surface)
	{
		VulkanBackend::GetSurfaceCapabilities(backendData, surfaceData);
		VulkanBackend::GetSurfaceExtent(backendData, surfaceData);

		surfaceData.width = surfaceData.surfaceExtent.width;
		surfaceData.height = surfaceData.surfaceExtent.height;

		p_->commonFrameData.swapchain = VulkanBackend::RecreateSwapchain(backendData, surfaceData,
			p_->commonFrameData.swapchain, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

		p_->commonFrameData.swapchainImages.clear();
		VulkanBackend::GetSwapchainImages(backendData, p_->commonFrameData.swapchain, p_->commonFrameData.swapchainImages);

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		for (int i = 0; i < p_->commonFrameData.framesInFlightCount; ++i)
		{
			p_->commonFrameData.swapchainImageViews[i] = VulkanBackend::CreateImageView2D(backendData,
				p_->commonFrameData.swapchainImages[i], surfaceData.surfaceFormat.format, subresourceRange);
		}
	}

	p_->frameGraph.Resize(p_->commonFrameData);

	for (int c = 0; c < p_->commonFrameData.framesInFlightCount; ++c)
	{
		p_->commonFrameData.commandBuffers[c].dirty = true;
	}
}

void HawkEye::Pipeline::Refresh()
{
	for (int c = 0; c < p_->commonFrameData.framesInFlightCount; ++c)
	{
		p_->commonFrameData.commandBuffers[c].dirty = true;
	}
}

void HawkEye::Pipeline::ReleaseResources()
{
	vkDeviceWaitIdle(p_->commonFrameData.backendData->logicalDevice);

	VulkanBackend::ResetCommandPool(*p_->commonFrameData.backendData, p_->commonFrameData.commandPool);
}

uint64_t HawkEye::Pipeline::GetPresentedFrame() const
{
	return p_->commonFrameData.currentFrame;
}

uint64_t HawkEye::Pipeline::GetFramesInFlight() const
{
	return p_->frameFences.size();
}

uint64_t HawkEye::Pipeline::GetUUID() const
{
	return (uint64_t)this;
}

void HawkEye::Pipeline::SetUniform(const std::string& nodeName, const std::string& name, HTexture texture)
{
	std::shared_ptr<TextureUpdateData> textureData = std::make_shared<TextureUpdateData>(nodeName, name, texture);
	for (int f = 0; f < p_->commonFrameData.framesInFlightCount; ++f)
	{
		p_->textureUpdateData[f].push(textureData);
	}
}

void HawkEye::Pipeline::SetUniform(const std::string& nodeName, const std::string& name, HBuffer buffer)
{
	std::shared_ptr<BufferUpdateData> bufferData = std::make_shared<BufferUpdateData>(nodeName, name, buffer);
	for (int f = 0; f < p_->commonFrameData.framesInFlightCount; ++f)
	{
		p_->bufferUpdateData[f].push(bufferData);
	}
}

void HawkEye::Pipeline::SetUniformImpl(const std::string& nodeName, const std::string& name, void* data, int dataSize)
{
	// TODO: Make safe.
	// TODO: Handle multiple updates of the same uniform in the same frame.
	void* dataCopy = malloc(dataSize);
	memcpy(dataCopy, data, dataSize);
	std::shared_ptr<PreallocatedUpdateData> preallocatedData = std::make_shared<PreallocatedUpdateData>(
		nodeName, name, dataCopy, dataSize);
	for (int f = 0; f < p_->commonFrameData.framesInFlightCount; ++f)
	{
		p_->preallocatedUpdateData[f].push(preallocatedData);
	}
}

HawkEye::HMaterial HawkEye::Pipeline::CreateMaterialImpl(const std::string& nodeName, void* data, int dataSize)
{
	return p_->frameGraph.CreateMaterial(nodeName, data, dataSize);
}

void HawkEye::Pipeline::UpdateUniforms(int frameInFlight)
{
	while (!p_->preallocatedUpdateData[frameInFlight].empty())
	{
		auto preallocatedData = p_->preallocatedUpdateData[frameInFlight].front();
		p_->preallocatedUpdateData[frameInFlight].pop();
		p_->frameGraph.UpdatePreallocatedUniformData(preallocatedData->nodeName, preallocatedData->name, frameInFlight,
			preallocatedData->data, preallocatedData->dataSize);
	}

	while (!p_->bufferUpdateData[frameInFlight].empty())
	{
		auto bufferData = p_->bufferUpdateData[frameInFlight].front();
		p_->bufferUpdateData[frameInFlight].pop();
		p_->frameGraph.UpdateStorageBuffer(bufferData->nodeName, bufferData->name, frameInFlight, bufferData->buffer);
	}

	while (!p_->textureUpdateData[frameInFlight].empty())
	{
		auto textureData = p_->textureUpdateData[frameInFlight].front();
		p_->textureUpdateData[frameInFlight].pop();
		p_->frameGraph.UpdateTexture(textureData->nodeName, textureData->name, frameInFlight, textureData->texture);
	}
}

VkFormat PipelineUtils::GetAttributeFormat(const VertexAttribute& vertexAttribute)
{
	return (VkFormat)(VK_FORMAT_R32_UINT + (vertexAttribute.byteCount / 4 - 1) * 3 + (int)vertexAttribute.type);
}
