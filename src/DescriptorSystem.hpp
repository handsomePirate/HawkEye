#pragma once
#include "YAMLConfiguration.hpp"
#include <vulkan/vulkan.hpp>

class DescriptorSystem
{
public:
	DescriptorSystem() = default;
	~DescriptorSystem() = default;

	static VkDescriptorSetLayout InitSetLayout(VulkanBackend::BackendData* backendData,
		const std::vector<UniformData>& uniformData);

	void Init(VulkanBackend::BackendData* backendData, HawkEye::HRendererData rendererData,
		const std::vector<UniformData>& uniformData, int framesInFlightCount, VkDescriptorSetLayout descriptorSetLayout);
	void Shutdown();

	VkDescriptorSet GetSet(int frameInFlight) const;

	void UpdatePreallocated(const std::string& name, int frameInFlight, void* data, int dataSize);
	void UpdateBuffer(const std::string& name, int frameInFlight, HawkEye::HBuffer buffer);
	void UpdateTexture(const std::string& name, int frameInFlight, HawkEye::HTexture texture);

private:
	VulkanBackend::BackendData* backendData;
	HawkEye::HRendererData rendererData;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> descriptorSets;
	std::map<std::string, HawkEye::HBuffer> preallocatedBuffers;
	std::map<std::string, int> resourceBindings;
};
