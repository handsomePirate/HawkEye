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

	HawkEye::HRendererData rendererData = HawkEye::Initialize(backendConfigFile.c_str());

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

	std::array<HawkEye::HTexture, 1> textures;
	for (int t = 0; t < textures.size(); ++t)
	{
		textures[t] = HawkEye::UploadTexture(rendererData, image.data(), (int)image.size(), width, height, HawkEye::TextureFormat::RGBA,
			HawkEye::ColorCompression::SRGB, HawkEye::TextureCompression::None, false);
	}
	
	std::vector<float> vertexBufferData = 
	{
		-1.f, -1.f, 0.f,
		1.f, -1.f, 0.f,
		-1.f, 1.f, 0.f
	};
	HawkEye::HBuffer vertexBuffer = HawkEye::UploadBuffer(rendererData, vertexBufferData.data(), (int)vertexBufferData.size() * sizeof(float),
		HawkEye::BufferUsage::Vertex, HawkEye::BufferType::DeviceLocal);
	
	HawkEye::Pipeline::DrawBuffer drawBuffer{ vertexBuffer, nullptr };

	renderingPipeline.UseBuffers(&drawBuffer, 1);

	while (!testWindow.ShouldClose())
	{
		testWindow.PollMessages();
		renderingPipeline.DrawFrame();
	}

	// Releasing resources.
	renderingPipeline.UseBuffers(nullptr, 0);

	// Waiting for frames in flight to finish using the resources.
	for (int f = 0; f < renderingPipeline.GetFramesInFlight(); ++f)
	{
		renderingPipeline.DrawFrame();
	}

	HawkEye::DeleteBuffer(rendererData, vertexBuffer);

	for (int t = 0; t < textures.size(); ++t)
	{
		HawkEye::DeleteTexture(rendererData, textures[t]);
	}

	renderingPipeline.Shutdown();

	HawkEye::Shutdown();

	return 0;
}
