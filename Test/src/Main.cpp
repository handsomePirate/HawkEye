#include "Camera.hpp"
#include <HawkEye/HawkEyeAPI.hpp>
#include <EverViewport/WindowAPI.hpp>
#include <SoftwareCore/Input.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <SoftwareCore/Logger.hpp>
#include <HawkEye/Logger.hpp>
#include <array>
#include <chrono>
#include <future>
#include <iostream>

const int windowWidth = 720;
const int windowHeight = 480;

HawkEye::Pipeline renderingPipeline1;
ControllerModule::Scene::Camera camera1(windowWidth / float(windowHeight));
EverViewport::Window* testWindow1 = nullptr;


#ifdef SECOND_WINDOW
HawkEye::Pipeline renderingPipeline2;
ControllerModule::Scene::Camera camera2(windowWidth / float(windowHeight));
EverViewport::Window* testWindow2 = nullptr;
#endif

// Callbacks.

void Print(const char* message, Core::LoggerSeverity severity)
{
	const char* traceSev = "[Trace] ";
	const char* debugSev = "[Debug] ";
	const char* infoSev = "[Info] ";
	const char* warnSev = "[Warn] ";
	const char* errorSev = "[Error] ";
	const char* fatalSev = "[Fatal] ";

	switch (severity)
	{
	case Core::LoggerSeverity::Trace:
		std::cout << traceSev;
		break;
	case Core::LoggerSeverity::Debug:
		std::cout << debugSev;
		break;
	case Core::LoggerSeverity::Info:
		std::cout << infoSev;
		break;
	case Core::LoggerSeverity::Warn:
		std::cout << warnSev;
		break;
	case Core::LoggerSeverity::Error:
		std::cout << errorSev;
		break;
	case Core::LoggerSeverity::Fatal:
		std::cout << fatalSev;
		break;
	}

	std::cout << message;
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
	if (renderingPipeline1.Configured())
	{
		renderingPipeline1.DrawFrame();
	}
}

void Resize(int width, int height)
{
	if (width != 0 && height != 0)
	{
		camera1.SetAspect(width / float(height));
		camera1.UpdateViewProjectionMatrices();
	}
	if (renderingPipeline1.Configured())
	{
		Eigen::Matrix4f viewProjectionMatrix = camera1.GetProjectionMatrix() * camera1.GetViewMatrix();
		renderingPipeline1.SetUniform("camera", viewProjectionMatrix, 1);
		renderingPipeline1.Resize(width, height);
	}
}

#ifdef SECOND_WINDOW
void Render2()
{
	if (renderingPipeline2.Configured())
	{
		renderingPipeline2.DrawFrame();
	}
}

void Resize2(int width, int height)
{
	if (width != 0 && height != 0)
	{
		camera2.SetAspect(width / float(height));
		camera2.UpdateViewProjectionMatrices();
	}
	if (renderingPipeline2.Configured())
	{
		Eigen::Matrix4f viewProjectionMatrix = camera2.GetProjectionMatrix() * camera2.GetViewMatrix();
		renderingPipeline2.SetUniform("camera", viewProjectionMatrix, 0);
		renderingPipeline2.Resize(width, height);
	}
}
#endif

void HandleInput(float timeDelta)
{
	HawkEye::Pipeline* renderingPipeline = &renderingPipeline1;
	ControllerModule::Scene::Camera* camera = &camera1;

	bool window1Focused = testWindow1->InFocus();
#ifdef SECOND_WINDOW
	bool window2Focused = testWindow2->InFocus();

	if (window2Focused)
	{
		renderingPipeline = &renderingPipeline2;
		camera = &camera2;
	}
	else if (!window1Focused)
	{
		return;
	}
#endif

	static uint16_t lastMouseX = 0;
	static uint16_t lastMouseY = 0;

	bool forward = CoreInput.IsKeyPressed(Core::Input::Keys::W) || CoreInput.IsKeyPressed(Core::Input::Keys::Up);
	bool back = CoreInput.IsKeyPressed(Core::Input::Keys::S) || CoreInput.IsKeyPressed(Core::Input::Keys::Down);
	bool left = CoreInput.IsKeyPressed(Core::Input::Keys::A) || CoreInput.IsKeyPressed(Core::Input::Keys::Left);
	bool right = CoreInput.IsKeyPressed(Core::Input::Keys::D) || CoreInput.IsKeyPressed(Core::Input::Keys::Right);

	bool up = CoreInput.IsKeyPressed(Core::Input::Keys::R);
	bool down = CoreInput.IsKeyPressed(Core::Input::Keys::F);

	bool shift = CoreInput.IsKeyPressed(Core::Input::Keys::Shift);

	if (forward || back || left || right || up || down)
	{
		//const float moveSensitivity = shift ? 120.f : 50.f;
		const float moveSensitivity = .002f;
		const float forwardDelta =
			((forward ? moveSensitivity : -moveSensitivity) +
				(back ? -moveSensitivity : moveSensitivity))
			* timeDelta;
		const float rightDelta =
			((right ? moveSensitivity : -moveSensitivity) +
				(left ? -moveSensitivity : moveSensitivity))
			* timeDelta;
		const float upDelta =
			((up ? -moveSensitivity : moveSensitivity) +
				(down ? moveSensitivity : -moveSensitivity))
			* timeDelta;

		camera->TranslateLocal({ rightDelta, upDelta, forwardDelta });
	}

	uint16_t mouseX = CoreInput.GetMouseX();
	uint16_t mouseY = CoreInput.GetMouseY();
	const bool isMousePressedLeft = CoreInput.IsMouseButtonPressed(Core::Input::MouseButtons::Left);
	const float mouseSensitivity = 0.005f;

	if (isMousePressedLeft)
	{
		const int deltaX = int(lastMouseX) - int(mouseX);
		const int deltaY = int(lastMouseY) - int(mouseY);

		const float xMove = mouseSensitivity * deltaX * timeDelta;
		const float yMove = mouseSensitivity * deltaY * timeDelta;

		camera->Rotate({ 0, 1, 0 }, -xMove);
		camera->RotateLocal({ 1, 0, 0 }, yMove);
	}

	camera->UpdateViewProjectionMatrices();

	if (renderingPipeline->Configured())
	{
		Eigen::Matrix4f viewProjectionMatrix = camera->GetProjectionMatrix() * camera->GetViewMatrix();
		renderingPipeline->SetUniform("camera", viewProjectionMatrix, 1);
	}

	lastMouseX = mouseX;
	lastMouseY = mouseY;
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
		testWindow1 = new EverViewport::Window(50, 50, windowWidth, windowHeight, "test 1", windowCallbacks);
#ifdef SECOND_WINDOW
		EverViewport::WindowCallbacks windowCallbacks2{ Render2, Resize2 };
		testWindow2 = new EverViewport::Window(50, 50, windowWidth, windowHeight, "test 2", windowCallbacks2);
#endif

		// Pipeline.

		renderingPipeline1.Configure(rendererData, frontendConfigFile.c_str(), windowWidth, windowHeight,
			testWindow1->GetWindowHandle(), testWindow1->GetProgramConnection());
#ifdef SECOND_WINDOW
		renderingPipeline2.Configure(rendererData, frontendConfigFile.c_str(), windowWidth, windowHeight,
			testWindow2->GetWindowHandle(), testWindow2->GetProgramConnection());
#endif

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
		HawkEye::HMaterial material1 = renderingPipeline1.CreateMaterial(materialData1, 1);
#ifdef SECOND_WINDOW
		HawkEye::HMaterial material21 = renderingPipeline2.CreateMaterial(materialData1, 0);
#endif

		TextureMaterial materialData2{ myTexture };
		HawkEye::HMaterial material2 = renderingPipeline1.CreateMaterial(materialData2, 1);

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

		renderingPipeline1.UseBuffers(drawBuffers, drawBufferCount, 1);
		Eigen::Matrix4f viewProjectionMatrix = camera1.GetProjectionMatrix() * camera1.GetViewMatrix();
		renderingPipeline1.SetUniform("camera", viewProjectionMatrix, 1);
		
#ifdef SECOND_WINDOW
		renderingPipeline2.UseBuffers(drawBuffers, 1, 0);
		Eigen::Matrix4f viewProjectionMatrix2 = camera2.GetProjectionMatrix() * camera2.GetViewMatrix();
		renderingPipeline2.SetUniform("camera", viewProjectionMatrix2, 0);
#endif

		// Rendering loop.

		//uint32_t test = 0;
		float timeDelta = 1;
		auto before = std::chrono::high_resolution_clock::now();
#ifdef SECOND_WINDOW
		while (!testWindow1->ShouldClose() && !testWindow2->ShouldClose())
#else
		while (!testWindow1->ShouldClose())
#endif
		{
			testWindow1->PollMessages();
			//renderingPipeline.UseBuffers(&drawBuffers[test], 1);
			//test = (test + 1) % 2;
			renderingPipeline1.DrawFrame();
#ifdef SECOND_WINDOW
			testWindow2->PollMessages();
			renderingPipeline2.DrawFrame();
#endif

			auto now = std::chrono::high_resolution_clock::now();
			timeDelta = std::chrono::duration<float, std::milli>(now - before).count();
			before = std::chrono::high_resolution_clock::now();
			HandleInput(timeDelta);
		}

		// Releasing of resources.

		renderingPipeline1.ReleaseResources();
#ifdef SECOND_WINDOW
		renderingPipeline2.ReleaseResources();
#endif

		HawkEye::DeleteBuffer(rendererData, vertexBuffer0);
		HawkEye::DeleteBuffer(rendererData, vertexBuffer1);
		HawkEye::DeleteBuffer(rendererData, indexBuffer);
		HawkEye::DeleteBuffer(rendererData, instanceBuffer);

		for (int t = 0; t < textures.size(); ++t)
		{
			HawkEye::DeleteTexture(rendererData, textures[t]);
		}
		HawkEye::DeleteTexture(rendererData, myTexture);

#ifdef SECOND_WINDOW
		renderingPipeline2.Shutdown();
#endif
		renderingPipeline1.Shutdown();

		HawkEye::Shutdown();

		delete testWindow1;
#ifdef SECOND_WINDOW
		delete testWindow2;
#endif
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}

	return 0;
}
