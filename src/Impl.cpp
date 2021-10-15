#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>

void Print(const char* message, Core::LoggerSeverity severity)
{
	printf(message);
}

void HawkEye::Test()
{
	VulkanLogger.SetNewOutput(&Print);
	
	Core::Filesystem filesystem(__argv[0]);
	auto configFile = filesystem.GetAbsolutePath("../../../ext/VulkanBackend/Test/testfile.yml");

	auto initialized = VulkanBackend::Initialize(configFile.c_str());
	VulkanBackend::Shutdown(initialized);
}
