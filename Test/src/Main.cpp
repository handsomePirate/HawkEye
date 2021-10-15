#include <HawkEye/HawkEyeAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <SoftwareCore/Logger.hpp>
#include <HawkEye/Logger.hpp>

void Print(const char* message, Core::LoggerSeverity severity)
{
	printf(message);
}

int main(int argc, char* argv[])
{
	Core::Filesystem filesystem(argv[0]);
	auto backendConfigFile = filesystem.GetAbsolutePath("../../../ext/VulkanBackend/Test/testfile.yml");
	auto frontendConfigFile = filesystem.GetAbsolutePath("../../testfile.yml");

	VulkanLogger.SetNewOutput(&Print);

	auto hawkEyeData = HawkEye::Initialize(backendConfigFile.c_str());
	
	HawkEye::Pipeline renderingPipeline;
	renderingPipeline.Configure(hawkEyeData, frontendConfigFile.c_str());

	HawkEye::Shutdown();

	return 0;
}
