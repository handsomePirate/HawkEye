#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>

void HawkEye::Pipeline::Configure(RendererData rendererData, const char* configFile,
	void* windowHandle, void* connection)
{
	VulkanBackend::Initialized* backendData = (VulkanBackend::Initialized*)rendererData;
	

}

void HawkEye::Pipeline::DrawFrame(RendererData rendererData)
{

}

void HawkEye::Pipeline::Resize(RendererData rendererData)
{

}
