#include "Framebuffer.hpp"

Target FramebufferUtils::CreateDepthTarget(const VulkanBackend::BackendData& backendData,
	const VulkanBackend::SurfaceData& surfaceData, int width, int height)
{
	Target target;
	target.image = VulkanBackend::CreateImage2D(backendData, width, height, 1, 1,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT /*| VK_IMAGE_USAGE TRANSFER_SRC_BIT*/,
		surfaceData.depthFormat, VMA_MEMORY_USAGE_GPU_ONLY);

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	subresourceRange.layerCount = 1;
	subresourceRange.levelCount = 1;

	target.imageView = VulkanBackend::CreateImageView2D(backendData, target.image.image,
		surfaceData.depthFormat, subresourceRange);

	target.inherited = false;

	return target;
}

void FramebufferUtils::DestroyTarget(const VulkanBackend::BackendData& backendData, Target& target)
{
	if (target.inherited)
	{
		target.inherited = false;
		target.image.image = VK_NULL_HANDLE;
		target.image.allocation = nullptr;
		target.imageView = VK_NULL_HANDLE;
		return;
	}
	VulkanBackend::DestroyImageView(backendData, target.imageView);
	VulkanBackend::DestroyImage(backendData, target.image);
}
