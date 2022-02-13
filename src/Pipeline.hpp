#pragma once
#include "HawkEye/HawkEyeAPI.hpp"
#include "FrameGraph/FrameGraph.hpp"
#include "YAMLConfiguration.hpp"
#include "Framebuffer.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <queue>

// TODO: Thread-safe queue
struct PreallocatedUpdateData
{
	std::string nodeName;
	std::string name;
	void* data;
	int dataSize;

	PreallocatedUpdateData(const std::string& nodeName, const std::string& name,
		void* data, int dataSize)
		: nodeName(nodeName), name(name), data(data), dataSize(dataSize) {}

	~PreallocatedUpdateData()
	{
		free(data);
	}
};

struct TextureUpdateData
{
	std::string nodeName;
	std::string name;
	HawkEye::HTexture texture;

	TextureUpdateData(const std::string& nodeName, const std::string& name, HawkEye::HTexture texture)
		: nodeName(nodeName), name(name), texture(texture) {}
};

struct BufferUpdateData
{
	std::string nodeName;
	std::string name;
	HawkEye::HBuffer buffer;

	BufferUpdateData(const std::string& nodeName, const std::string& name, HawkEye::HBuffer buffer)
		: nodeName(nodeName), name(name), buffer(buffer) {}
};

struct HawkEye::Pipeline::Private
{
	bool configured = false;
	CommonFrameData commonFrameData;
	VkSemaphore graphicsSemaphore = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	std::vector<VkFence> frameFences;
	FrameGraph frameGraph;

	std::vector<std::queue<std::shared_ptr<PreallocatedUpdateData>>> preallocatedUpdateData;
	std::vector<std::queue<std::shared_ptr<TextureUpdateData>>> textureUpdateData;
	std::vector<std::queue<std::shared_ptr<BufferUpdateData>>> bufferUpdateData;
};

namespace PipelineUtils
{
	VkFormat GetAttributeFormat(const VertexAttribute& vertexAttribute);
}
