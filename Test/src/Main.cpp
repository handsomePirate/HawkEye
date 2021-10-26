#include <HawkEye/HawkEyeAPI.hpp>
#include <EverViewport/WindowAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <SoftwareCore/Logger.hpp>
#include <HawkEye/Logger.hpp>
#include <array>

HawkEye::Pipeline renderingPipeline;

void Print(const char* message, Core::LoggerSeverity severity)
{
	printf(message);
}

void Render()
{
	renderingPipeline.DrawFrame();
}

void Resize(int width, int height)
{
	renderingPipeline.Resize(width, height);
}

int main(int argc, char* argv[])
{
	Core::Filesystem filesystem(argv[0]);
	auto backendConfigFile = filesystem.GetAbsolutePath("../../../ext/VulkanBackend/Test/testfile.yml");
	auto frontendConfigFile = filesystem.GetAbsolutePath("../../testfile.yml");

	VulkanLogger.SetNewOutput(&Print);

	HawkEye::RendererData rendererData = HawkEye::Initialize(backendConfigFile.c_str());

	EverViewport::WindowCallbacks windowCallbacks{Render, Resize};
	const int windowWidth = 720;
	const int windowHeight = 480;
	EverViewport::Window testWindow(50, 50, windowWidth, windowHeight, "test", windowCallbacks);
	
	renderingPipeline.Configure(rendererData, frontendConfigFile.c_str(), windowWidth, windowHeight,
		testWindow.GetWindowHandle(), testWindow.GetProgramConnection());

	
	// | R | R | G | G |
	// | R | R | G | G |
	// | B | B |   |   |
	// | B | B |   |   |

	std::array<unsigned char, 64> image =
	{
		255, 0, 0, 255,
		0, 255, 0, 255,
		0, 255, 0, 255,
		0, 255, 0, 255,

		0, 255, 0, 255,
		255, 0, 0, 255,
		0, 255, 0, 255,
		0, 255, 0, 255,


		0, 0, 255, 255,
		0, 0, 255, 255,
		255, 0, 0, 255,
		255, 0, 0, 255,

		0, 0, 255, 255,
		0, 0, 255, 255,
		255, 0, 0, 255,
		255, 0, 0, 255
	};

	HawkEye::HTexture texture = HawkEye::UploadTexture(rendererData, image.data(), (int)image.size(), 4, 4, HawkEye::TextureFormat::RGBA,
		HawkEye::ColorCompression::SRGB, HawkEye::TextureCompression::None, true);

	while (!testWindow.ShouldClose())
	{
		testWindow.PollMessages();
		renderingPipeline.DrawFrame();
	}

	HawkEye::DeleteTexture(rendererData, texture);


	renderingPipeline.Shutdown();

	HawkEye::Shutdown();

	return 0;
}
