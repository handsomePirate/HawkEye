#pragma once

namespace HawkEye
{
	typedef struct RendererData_t* RendererData;

	RendererData Initialize(const char* backendConfigFile);
	void Shutdown();

	class Pipeline
	{
	public:
		Pipeline();
		~Pipeline();

		void Configure(RendererData rendererData, const char* configFile, int width, int height,
			void* windowHandle = nullptr, void* windowConnection = nullptr);
		void Shutdown();

		void DrawFrame();
		void Resize(int width, int height);
	private:
		struct Private;
		Private* p_;
	};

	typedef struct HTexture_t* HTexture;
	enum class TextureFormat
	{
		Gray,
		GrayAlpha,
		RGB,
		RGBA
	};

	enum class ColorCompression
	{
		None,
		SRGB
	};

	enum class TextureCompression
	{
		None
	};

	HTexture UploadTexture(RendererData rendererData, unsigned char* data, int dataSize, int width, int height,
		TextureFormat format, ColorCompression colorCompression, TextureCompression textureCompression, bool generateMips);
	void DeleteTexture(RendererData rendererData, HTexture& texture);
}
