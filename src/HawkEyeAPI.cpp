#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>

static VulkanBackend::Initialized backendData{};

HawkEye::RendererData HawkEye::Initialize(const char* backendConfigFile)
{
    backendData = VulkanBackend::Initialize(backendConfigFile);
    return (HawkEye::RendererData)&backendData;
}

void HawkEye::Shutdown()
{
    VulkanBackend::Shutdown(backendData);
}
