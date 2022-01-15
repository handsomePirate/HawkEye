#pragma once
#include <VulkanBackend/VulkanBackendAPI.hpp>

struct Target
{
	VulkanBackend::Image image;
	VkImageView imageView;
	bool inherited;
};

namespace FramebufferUtils
{
	Target CreateColorTarget(const VulkanBackend::BackendData& backendData,
		const VulkanBackend::SurfaceData& surfaceData, VkFormat targetFormat = VK_FORMAT_UNDEFINED,
		bool retargetSource = false);

	Target CreateDepthTarget(const VulkanBackend::BackendData& backendData,
		const VulkanBackend::SurfaceData& surfaceData, VkFormat targetFormat = VK_FORMAT_UNDEFINED);

	void DestroyTarget(const VulkanBackend::BackendData& backendData, Target& target);
}
