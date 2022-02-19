#pragma once
#include "FrameGraph/NodeStructs.hpp"
#include <VulkanBackend/VulkanBackendAPI.hpp>

namespace FramebufferUtils
{
	Target CreateColorTarget(const VulkanBackend::BackendData& backendData,
		const VulkanBackend::SurfaceData& surfaceData, ImageFormat targetFormat);

	Target CreateDepthTarget(const VulkanBackend::BackendData& backendData,
		const VulkanBackend::SurfaceData& surfaceData, ImageFormat targetFormat);

	void DestroyTarget(const VulkanBackend::BackendData& backendData, Target& target);
}
