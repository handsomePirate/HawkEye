#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>

static VulkanBackend::BackendData backendData{};

HawkEye::HRendererData HawkEye::Initialize(const char* backendConfigFile)
{
    backendData = VulkanBackend::Initialize(backendConfigFile);
    return (HawkEye::HRendererData)&backendData;
}

void HawkEye::Shutdown()
{
    VulkanBackend::Shutdown(backendData);
}
