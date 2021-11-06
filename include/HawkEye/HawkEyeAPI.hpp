#pragma once

namespace HawkEye
{
	typedef struct HRendererData_t* HRendererData;

	HRendererData Initialize(const char* backendConfigFile);
	void Shutdown();

	class Pipeline
	{
	public:
		Pipeline();
		~Pipeline();

		// TODO: Vertex attributes.
		void Configure(HRendererData rendererData, const char* configFile, int width, int height,
			void* windowHandle = nullptr, void* windowConnection = nullptr);
		void Shutdown();

		void DrawFrame();
		void Resize(int width, int height);
	private:
		struct Private;
		Private* p_;
	};

	// ======================== Textures =======================

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

	enum class TextureQueue
	{
		General,
		Compute
	};

	HTexture UploadTexture(HRendererData rendererData, void* data, int dataSize, int width, int height,
		TextureFormat format, ColorCompression colorCompression, TextureCompression textureCompression,
		bool generateMips, TextureQueue usage = TextureQueue::General);
	void DeleteTexture(HRendererData rendererData, HTexture& texture);

	void WaitForUpload(HRendererData rendererData, HTexture texture);
	bool UploadFinished(HRendererData rendererData, HTexture texture);

	// ======================== Buffers ========================

	typedef struct HBuffer_t* HBuffer;
	enum class BufferUsage
	{
		Vertex,
		Index,
		Uniform,
		Storage
	};

	enum class BufferType
	{
		Mapped,
		DeviceLocal
	};

	enum class BufferQueue
	{
		General,
		Compute
	};

	HBuffer UploadBuffer(HRendererData rendererData, void* data, int dataSize, BufferUsage usage, BufferType type,
		BufferQueue bufferQueue = BufferQueue::General);
	void DeleteBuffer(HRendererData rendererData, HBuffer& buffer);

	void UpdateBuffer(HRendererData rendererData, HBuffer buffer, void* data, int dataSize);

	void WaitForUpload(HRendererData rendererData, HBuffer buffer);
	bool UploadFinished(HRendererData rendererData, HBuffer buffer);

	// Index buffer.
	// Uniform buffer.
	// Storage buffer.
}
