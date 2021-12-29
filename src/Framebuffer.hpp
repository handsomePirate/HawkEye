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
		const VulkanBackend::SurfaceData& surfaceData, bool retargetSource = false);

	Target CreateDepthTarget(const VulkanBackend::BackendData& backendData,
		const VulkanBackend::SurfaceData& surfaceData);

	void DestroyTarget(const VulkanBackend::BackendData& backendData, Target& target);
}
