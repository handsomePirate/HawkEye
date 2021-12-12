#include "Camera.hpp"
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
const int windowWidth = 720;
const int windowHeight = 480;
ControllerModule::Scene::Camera camera(windowWidth / float(windowHeight));

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
	if (renderingPipeline.Configured())
	{
		renderingPipeline.DrawFrame();
	}
}

void Resize(int width, int height)
{
	if (width != 0 && height != 0)
	{
		camera.SetAspect(width / float(height));
		camera.UpdateViewProjectionMatrices();
	}
	if (renderingPipeline.Configured())
	{
		Eigen::Matrix4f viewProjectionMatrix = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		renderingPipeline.SetUniform("camera", viewProjectionMatrix);
		renderingPipeline.Resize(width, height);
	}
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
		EverViewport::Window testWindow(50, 50, windowWidth, windowHeight, "test", windowCallbacks);

		// Pipeline.

		renderingPipeline.Configure(rendererData, frontendConfigFile.c_str(), windowWidth, windowHeight,
			testWindow.GetWindowHandle(), testWindow.GetProgramConnection());

		// Texture (gradient).

		constexpr int width = 4096;
		constexpr int height = 4096;
		constexpr int dataSize = width * height * 4;
		std::vector<unsigned char> image(dataSize);
		std::vector<unsigned char> image2(dataSize);
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				int level = (int)std::round(x * 255 / (float)(width - 1));
				image[(y * width + x) * 4 + 0] = level;
				image[(y * width + x) * 4 + 1] = 0;
				image[(y * width + x) * 4 + 2] = 0;
				image[(y * width + x) * 4 + 3] = 255;

				image2[(y * width + x) * 4 + 0] = 0;
				image2[(y * width + x) * 4 + 1] = 0;
				image2[(y * width + x) * 4 + 2] = level;
				image2[(y * width + x) * 4 + 3] = 255;
			}
		}

		std::array<HawkEye::HTexture, 1> textures;
		for (int t = 0; t < textures.size(); ++t)
		{
			textures[t] = HawkEye::UploadTexture(rendererData, image.data(), (int)image.size(), width, height, HawkEye::TextureFormat::RGBA,
				HawkEye::ColorCompression::SRGB, HawkEye::TextureCompression::None, false);
		}

		HawkEye::HTexture myTexture = HawkEye::UploadTexture(rendererData, image2.data(), (int)image2.size(), width, height, HawkEye::TextureFormat::RGBA,
			HawkEye::ColorCompression::SRGB, HawkEye::TextureCompression::None, false);

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

		std::vector<float> modelMatrix =
		{
			1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f
		};

		struct TextureMaterial
		{
			HawkEye::HTexture texture;
		};
		TextureMaterial materialData1{ textures[0] };
		HawkEye::HMaterial material1 = renderingPipeline.CreateMaterial(materialData1);

		TextureMaterial materialData2{ myTexture };
		HawkEye::HMaterial material2 = renderingPipeline.CreateMaterial(materialData2);

		HawkEye::HBuffer vertexBuffer0;
		HawkEye::HBuffer vertexBuffer1;
		HawkEye::HBuffer indexBuffer;
		HawkEye::HBuffer instanceBuffer;

		const int drawBufferCount = 2;
		HawkEye::Pipeline::DrawBuffer drawBuffers[drawBufferCount];
		vertexBuffer0 = HawkEye::UploadBuffer(rendererData, vertexBufferData0.data(), (int)vertexBufferData0.size() * sizeof(float),
			HawkEye::BufferUsage::Vertex, HawkEye::BufferType::DeviceLocal);

		vertexBuffer1 = HawkEye::UploadBuffer(rendererData, vertexBufferData1.data(), (int)vertexBufferData1.size() * sizeof(float),
			HawkEye::BufferUsage::Vertex, HawkEye::BufferType::DeviceLocal);

		indexBuffer = HawkEye::UploadBuffer(rendererData, indexBufferData.data(), (int)indexBufferData.size() * sizeof(uint32_t),
			HawkEye::BufferUsage::Index, HawkEye::BufferType::DeviceLocal);

		instanceBuffer = HawkEye::UploadBuffer(rendererData, modelMatrix.data(), (int)modelMatrix.size() * sizeof(float),
			HawkEye::BufferUsage::Vertex, HawkEye::BufferType::DeviceLocal);

		drawBuffers[0].vertexBuffer = vertexBuffer0;
		drawBuffers[0].indexBuffer = nullptr;
		drawBuffers[0].material = material1;
		drawBuffers[0].instanceBuffer = instanceBuffer;

		drawBuffers[1].vertexBuffer = vertexBuffer1;
		drawBuffers[1].indexBuffer = indexBuffer;
		drawBuffers[1].material = material2;
		drawBuffers[1].instanceBuffer = instanceBuffer;

		renderingPipeline.UseBuffers(drawBuffers, drawBufferCount);

		Eigen::Matrix4f viewProjectionMatrix = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		renderingPipeline.SetUniform("camera", viewProjectionMatrix);

		// Rendering loop.

		//uint32_t test = 0;

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
		HawkEye::DeleteBuffer(rendererData, instanceBuffer);

		for (int t = 0; t < textures.size(); ++t)
		{
			HawkEye::DeleteTexture(rendererData, textures[t]);
		}
		HawkEye::DeleteTexture(rendererData, myTexture);

		renderingPipeline.Shutdown();

		HawkEye::Shutdown();

	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}

	return 0;
}
