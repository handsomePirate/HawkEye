#include "HawkEye/HawkEyeAPI.hpp"
#include "YAMLConfiguration.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <VulkanBackend/ErrorCheck.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>
#include <regex>
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
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline computePipeline = VK_NULL_HANDLE;
	VkPipeline rasterizationPipeline = VK_NULL_HANDLE;
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

	VkFramebuffer framebuffer = VulkanBackend::CreateFramebuffer(device, width, height, renderPass, p_->swapchainImageViews);
	p_->framebuffer = framebuffer;

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

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		shaderStages.emplace_back();
		shaderStages.emplace_back();
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		// TODO: Shader modules.

		p_->rasterizationPipeline = VulkanBackend::CreateGraphicsPipeline(device, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
			VK_SAMPLE_COUNT_1_BIT, dynamiceStates, vertexInput, renderPass, pipelineLayout, shaderStages, pipelineCache);
	}
	else
	{
		// TODO: Compute version.
	}

	CoreLogDebug(VulkanLogger, "Pipeline: Configuration successful.");
}

void HawkEye::Pipeline::Shutdown()
{
	if (p_->backendData)
	{
		if (p_->swapchain)
		{
			VulkanBackend::DestroySwapchain(p_->backendData->logicalDevice, p_->swapchain);
		}
		if (p_->surfaceData->surface)
		{
			VulkanBackend::DestroySurface(p_->backendData->instance, p_->surfaceData->surface);
		}
	}
}

void HawkEye::Pipeline::DrawFrame(RendererData rendererData)
{

}

void HawkEye::Pipeline::Resize(RendererData rendererData)
{

}
