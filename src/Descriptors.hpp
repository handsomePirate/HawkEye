#pragma once
#include "Pipeline.hpp"
#include "Resources.hpp"
#include <vulkan/vulkan.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>

namespace DescriptorUtils
{
	VkDescriptorSetLayout GetSetLayout(const VulkanBackend::BackendData& backendData,
		const std::vector<UniformData>& uniformData);

	std::vector<VkDescriptorPoolSize> FilterPoolSizes(const std::vector<VkDescriptorPoolSize>& inPoolSizes);

	std::vector<VkDescriptorPoolSize> GetPoolSizes(HawkEye::HRendererData rendererData,
		const std::vector<UniformData>& uniformData, HawkEye::BufferType uniformBufferType,
		std::map<std::string, HawkEye::HBuffer>& uniformBuffers, const std::string& namePrepend = "",
		void* data = nullptr);

	void UpdateSets(HawkEye::HRendererData rendererData, const VulkanBackend::BackendData& backendData,
		const std::vector<UniformData>& uniformData, const std::vector<VkDescriptorPoolSize>& poolSizes,
		VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet, VkDescriptorSetLayout descriptorSetLayout,
		const std::map<std::string, HawkEye::HBuffer>& uniformBuffers, std::map<std::string, int>& uniformTextureBindings,
		std::map<std::string, int>& storageBufferBindings, const std::string& namePrepend = "", void* data = nullptr);
}
