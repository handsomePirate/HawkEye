#pragma once
#include <cstdint>
#include <string>

namespace HawkEye
{
	typedef struct HRendererData_t* HRendererData;
	typedef struct HTexture_t* HTexture;
	typedef struct HBuffer_t* HBuffer;
	typedef int HMaterial;

	HRendererData Initialize(const char* backendConfigFile);
	void Shutdown();

	class Pipeline
	{
	public:
		Pipeline();
		~Pipeline();

		void Configure(HRendererData rendererData, const char* configFile, int width, int height,
			void* windowHandle = nullptr, void* windowConnection = nullptr);
		void Shutdown();

		struct DrawBuffer
		{
			HBuffer vertexBuffer;
			HBuffer indexBuffer;
			HMaterial material;
		};
		
		// TODO: Layer.
		void UseBuffers(DrawBuffer* drawBuffers, int bufferCount);

		void DrawFrame();
		void Resize(int width, int height);

		// Unlock all resources used by the pipeline. They can be deleted afterwards.
		void ReleaseResources();

		uint64_t GetPresentedFrame() const;
		uint64_t GetFramesInFlight() const;

		uint64_t GetUUID() const;

		template<typename MaterialType>
		HMaterial CreateMaterial(MaterialType& material)
		{
			return CreateMaterialImpl(&material, sizeof(MaterialType));
		}

		template<typename MaterialType>
		HMaterial CreateMaterial(MaterialType&& material)
		{
			return CreateMaterialImpl(&material, sizeof(MaterialType));
		}

		// TODO: Delete material.

		void SetUniform(const std::string& name, HTexture texture);

		template<typename Type>
		void SetUniform(const std::string& name, Type& data)
		{
			SetUniformImpl(name, &data, sizeof(Type));
		}

		template<typename Type>
		void SetUniform(const std::string& name, Type&& data)
		{
			SetUniformImpl(name, &data, sizeof(Type));
		}

		struct Private;
		Private* p_;

	private:
		void SetUniformImpl(const std::string& name, void* data, int dataSize);
		HMaterial CreateMaterialImpl(void* data, int dataSize);
	};

	// ======================== Textures =======================

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
}
