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

		void DrawFrame(RendererData rendererData);
		void Resize(RendererData rendererData);
	private:
		struct Private;
		Private* p_;
	};
}
