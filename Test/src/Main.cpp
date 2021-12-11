#include <HawkEye/HawkEyeAPI.hpp>
#include <EverViewport/WindowAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <SoftwareCore/Logger.hpp>
#include <HawkEye/Logger.hpp>
#include <array>
#include <chrono>
#include <future>
#include <iostream>

HawkEye::Pipeline renderingPipeline;

// Callbacks.

void Print(const char* message, Core::LoggerSeverity severity)
{
	printf(message);
}

#ifdef _WIN32
#include <Windows.h>
void PrintWin32(const char* message, Core::LoggerSeverity severity)
{
	OutputDebugStringA(message);
}
#endif

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
	try
	{
		// Filesystem.

		Core::Filesystem filesystem(argv[0]);
		auto backendConfigFile = filesystem.GetAbsolutePath("../../../ext/VulkanBackend/Test/testfile.yml");
		auto frontendConfigFile = filesystem.GetAbsolutePath("../../testfile.yml");

		VulkanLogger.SetNewOutput(&Print);
#ifdef _WIN32
		VulkanLogger.SetNewOutput(&PrintWin32);
#endif

		// Renderer data.

		HawkEye::HRendererData rendererData = HawkEye::Initialize(backendConfigFile.c_str());

		// Window.

		EverViewport::WindowCallbacks windowCallbacks{ Render, Resize };
		const int windowWidth = 720;
		const int windowHeight = 480;
		EverViewport::Window testWindow(50, 50, windowWidth, windowHeight, "test", windowCallbacks);

		// Pipeline.

		renderingPipeline.Configure(rendererData, frontendConfigFile.c_str(), windowWidth, windowHeight,
			testWindow.GetWindowHandle(), testWindow.GetProgramConnection());

		// Texture (gradient).

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

		std::array<HawkEye::HTexture, 1> textures;
		auto start = std::chrono::high_resolution_clock::now();
		for (int t = 0; t < textures.size(); ++t)
		{
			textures[t] = HawkEye::UploadTexture(rendererData, image.data(), (int)image.size(), width, height, HawkEye::TextureFormat::RGBA,
				HawkEye::ColorCompression::SRGB, HawkEye::TextureCompression::None, false);
		}
		for (int t = 0; t < textures.size(); ++t)
		{
			HawkEye::WaitForUpload(rendererData, textures[t]);
		}
		auto end = std::chrono::high_resolution_clock::now();
		std::cout << "Texture upload took " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms." << std::endl;

		// Vertex and index buffers.

		std::vector<float> vertexBufferData0 =
		{

			/*pos*/-1.f, -1.f, 0.f,
			/*col*/1.f, 0.f, 0.f,
			/*uv*/ 0.f, 0.f,

			/*pos*/1.f, -1.f, 0.f,
			/*col*/1.f, 0.f, 0.f,
			/*uv*/ 1.f, 0.f,

			/*pos*/-1.f, 1.f, 0.f,
			/*col*/1.f, 0.f, 0.f,
			/*uv*/ 0.f, 1.f,
		};

		std::vector<float> vertexBufferData1 =
		{
			/*pos*/1.f, -1.f, 0.f,
			/*col*/0.f, 0.f, 1.f,
			/*uv*/ 1.f, 0.f,

			/*pos*/1.f, 1.f, 0.f,
			/*col*/0.f, 0.f, 1.f,
			/*uv*/ 1.f, 1.f,

			/*pos*/-1.f, 1.f, 0.f,
			/*col*/0.f, 0.f, 1.f,
			/*uv*/ 0.f, 1.f,
		};

		std::vector<uint32_t> indexBufferData =
		{
			0, 1, 2
		};

		HawkEye::HBuffer vertexBuffer0;
		HawkEye::HBuffer vertexBuffer1;
		HawkEye::HBuffer indexBuffer;
		HawkEye::Pipeline::DrawBuffer drawBuffers[2];
		vertexBuffer0 = HawkEye::UploadBuffer(rendererData, vertexBufferData0.data(), (int)vertexBufferData0.size() * sizeof(float),
			HawkEye::BufferUsage::Vertex, HawkEye::BufferType::DeviceLocal);

		vertexBuffer1 = HawkEye::UploadBuffer(rendererData, vertexBufferData1.data(), (int)vertexBufferData1.size() * sizeof(float),
			HawkEye::BufferUsage::Vertex, HawkEye::BufferType::DeviceLocal);

		indexBuffer = HawkEye::UploadBuffer(rendererData, indexBufferData.data(), (int)indexBufferData.size() * sizeof(uint32_t),
			HawkEye::BufferUsage::Index, HawkEye::BufferType::DeviceLocal);

		drawBuffers[0].vertexBuffer = vertexBuffer0;
		drawBuffers[0].indexBuffer = nullptr;
		drawBuffers[1].vertexBuffer = vertexBuffer1;
		drawBuffers[1].indexBuffer = indexBuffer;

		renderingPipeline.UseBuffers(drawBuffers, 2);

		// Rendering loop.

		//uint32_t test = 0;

		renderingPipeline.SetUniform("test", int(1));

		struct ComplexTest
		{
			int i1, i2;
		};
		renderingPipeline.SetUniform("complex", ComplexTest{ 2, -1 });

		renderingPipeline.SetUniform("texture", textures[0]);

		while (!testWindow.ShouldClose())
		{
			testWindow.PollMessages();
			//renderingPipeline.UseBuffers(&drawBuffers[test], 1);
			//test = (test + 1) % 2;
			renderingPipeline.DrawFrame();
		}

		// Releasing of resources.

		renderingPipeline.ReleaseResources();

		HawkEye::DeleteBuffer(rendererData, vertexBuffer0);
		HawkEye::DeleteBuffer(rendererData, vertexBuffer1);
		HawkEye::DeleteBuffer(rendererData, indexBuffer);

		for (int t = 0; t < textures.size(); ++t)
		{
			HawkEye::DeleteTexture(rendererData, textures[t]);
		}

		renderingPipeline.Shutdown();

		HawkEye::Shutdown();

	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}

	return 0;
}
