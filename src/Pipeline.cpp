#include "HawkEye/HawkEyeAPI.hpp"
#include "YAMLConfiguration.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>
#include <regex>
#include <memory>

struct HawkEye::Pipeline::Private
{
	std::unique_ptr<VulkanBackend::SurfaceData> surfaceData;
	VkCommandPool commandPool;
};

HawkEye::Pipeline::Pipeline()
	: p_(new Private) {}

HawkEye::Pipeline::~Pipeline()
{
	delete p_;
}

void HawkEye::Pipeline::Configure(RendererData rendererData, const char* configFile,
	void* windowHandle, void* connection)
{
	VulkanBackend::BackendData* backendData = (VulkanBackend::BackendData*)rendererData;
	
	YAML::Node configData = YAML::LoadFile(configFile);

	std::vector<PipelineLayer> layers;
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

	if (windowHandle)
	{
		// TODO: We have to prepare presentation.
		p_->surfaceData = std::make_unique<VulkanBackend::SurfaceData>();
		VulkanBackend::SurfaceData& surfaceData = *p_->surfaceData.get();
		VulkanBackend::CreateSurface(backendData->instance, surfaceData, windowHandle, connection);
		VulkanBackend::GetDepthFormat(backendData->physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceFormat(backendData->physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceCapabilities(backendData->physicalDevice, surfaceData);
		VulkanBackend::GetSurfaceExtent(backendData->physicalDevice, surfaceData);
		VulkanBackend::GetPresentMode(backendData->physicalDevice, surfaceData);
		VulkanBackend::GetSwapchainImageCount(surfaceData);

		VulkanBackend::FilterPresentQueues(*backendData, surfaceData);

		VulkanBackend::SelectPresentQueue(*backendData, surfaceData);
		VulkanBackend::SelectPresentComputeQueue(*backendData, surfaceData);
	}

	CoreLogDebug(VulkanLogger, "Pipeline: Configuration successful.");
}

void HawkEye::Pipeline::DrawFrame(RendererData rendererData)
{

}

void HawkEye::Pipeline::Resize(RendererData rendererData)
{

}
