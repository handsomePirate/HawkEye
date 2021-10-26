#include <HawkEye/HawkEyeAPI.hpp>
#include <EverViewport/WindowAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <SoftwareCore/Logger.hpp>
#include <HawkEye/Logger.hpp>
#include <array>
#include <chrono>

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

	// | R | R | G | G |
	// | R | R | G | G |
	// | B | B |   |   |
	// | B | B |   |   |

	constexpr int width = 4096;
	constexpr int height = 4096;
	constexpr int dataSize = width * height * 4;
	std::vector<unsigned char> image(dataSize);
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			int level = (int)std::round(x * 255 / (float)(width - 1));
			image[(y * width + x) * 4 + 0] = level;
			image[(y * width + x) * 4 + 1] = level;
			image[(y * width + x) * 4 + 2] = level;
			image[(y * width + x) * 4 + 3] = 255;
		}
	}

	EverViewport::WindowCallbacks windowCallbacks{Render, Resize};
	const int windowWidth = 720;
	const int windowHeight = 480;
	EverViewport::Window testWindow(50, 50, windowWidth, windowHeight, "test", windowCallbacks);
	
	renderingPipeline.Configure(rendererData, frontendConfigFile.c_str(), windowWidth, windowHeight,
		testWindow.GetWindowHandle(), testWindow.GetProgramConnection());

	auto start = std::chrono::high_resolution_clock::now();
	HawkEye::HTexture texture1 = HawkEye::UploadTexture(rendererData, image.data(), (int)image.size(), width, height, HawkEye::TextureFormat::RGBA,
		HawkEye::ColorCompression::SRGB, HawkEye::TextureCompression::None, true);
	auto end = std::chrono::high_resolution_clock::now();
	HawkEye::HTexture texture2 = HawkEye::UploadTexture(rendererData, image.data(), (int)image.size(), width, height, HawkEye::TextureFormat::RGBA,
		HawkEye::ColorCompression::None, HawkEye::TextureCompression::None, true);

	CoreLogDebug(VulkanLogger, "Test: Texture uploaded in %lld ms.", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

	while (!testWindow.ShouldClose())
	{
		testWindow.PollMessages();
		renderingPipeline.DrawFrame();
	}

	HawkEye::DeleteTexture(rendererData, texture1);
	HawkEye::DeleteTexture(rendererData, texture2);


	renderingPipeline.Shutdown();

	HawkEye::Shutdown();

	return 0;
}
