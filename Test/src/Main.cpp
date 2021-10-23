#include <HawkEye/HawkEyeAPI.hpp>
#include <EverViewport/WindowAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <SoftwareCore/Logger.hpp>
#include <HawkEye/Logger.hpp>

void Print(const char* message, Core::LoggerSeverity severity)
{
	printf(message);
}

void Render()
{

}

void Resize(int, int)
{

}

int main(int argc, char* argv[])
{
	Core::Filesystem filesystem(argv[0]);
	auto backendConfigFile = filesystem.GetAbsolutePath("../../../ext/VulkanBackend/Test/testfile.yml");
	auto frontendConfigFile = filesystem.GetAbsolutePath("../../testfile.yml");

	VulkanLogger.SetNewOutput(&Print);

	auto hawkEyeData = HawkEye::Initialize(backendConfigFile.c_str());

	EverViewport::WindowCallbacks windowCallbacks{Render, Resize};
	const int windowWidth = 720;
	const int windowHeight = 480;
	EverViewport::Window testWindow(50, 50, windowWidth, windowHeight, "test", windowCallbacks);
	
	HawkEye::Pipeline renderingPipeline;
	renderingPipeline.Configure(hawkEyeData, frontendConfigFile.c_str(), windowWidth, windowHeight,
		testWindow.GetWindowHandle(), testWindow.GetProgramConnection());

	renderingPipeline.DrawFrame(hawkEyeData);

	renderingPipeline.Shutdown();

	HawkEye::Shutdown();

	return 0;
}
