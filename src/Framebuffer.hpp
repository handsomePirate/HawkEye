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
		const VulkanBackend::SurfaceData& surfaceData, int width, int height, bool retargetSource = false);

	Target CreateDepthTarget(const VulkanBackend::BackendData& backendData,
		const VulkanBackend::SurfaceData& surfaceData, int width, int height);

	void DestroyTarget(const VulkanBackend::BackendData& backendData, Target& target);
}
