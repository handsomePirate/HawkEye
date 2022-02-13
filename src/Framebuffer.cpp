#include "Framebuffer.hpp"

Target FramebufferUtils::CreateColorTarget(const VulkanBackend::BackendData& backendData,
	const VulkanBackend::SurfaceData& surfaceData, ImageFormat targetFormat)
{
	Target target;
	VkFormat format = targetFormat.format;
	if (targetFormat.metadata != ImageFormat::Metadata::Specified)
	{
		if (targetFormat.metadata == ImageFormat::Metadata::ColorOptimal)
		{
			format = surfaceData.surfaceFormat.format;
		}
		else if (targetFormat.metadata == ImageFormat::Metadata::DepthOptimal)
		{
			CoreLogError(VulkanLogger, "Configuration: input/output format for color target is depth optimal");
			return target;
		}
	}
	target.image = VulkanBackend::CreateImage2D(backendData, surfaceData.width, surfaceData.height, 1, 1,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		format, VMA_MEMORY_USAGE_GPU_ONLY);

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.layerCount = 1;
	subresourceRange.levelCount = 1;

	target.imageView = VulkanBackend::CreateImageView2D(backendData, target.image.image,
		format, subresourceRange);

	target.inherited = false;

	return target;
}

Target FramebufferUtils::CreateDepthTarget(const VulkanBackend::BackendData& backendData,
	const VulkanBackend::SurfaceData& surfaceData, ImageFormat targetFormat)
{
	Target target;
	VkFormat format = targetFormat.format;
	if (targetFormat.metadata != ImageFormat::Metadata::Specified)
	{
		if (targetFormat.metadata == ImageFormat::Metadata::DepthOptimal)
		{
			format = surfaceData.depthFormat;
		}
		else if (targetFormat.metadata == ImageFormat::Metadata::ColorOptimal)
		{
			CoreLogError(VulkanLogger, "Configuration: input/output format for depth target is color optimal");
			return target;
		}
	}
	target.image = VulkanBackend::CreateImage2D(backendData, surfaceData.width, surfaceData.height, 1, 1,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT /*| VK_IMAGE_USAGE TRANSFER_SRC_BIT*/,
		format, VMA_MEMORY_USAGE_GPU_ONLY);

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	subresourceRange.layerCount = 1;
	subresourceRange.levelCount = 1;

	target.imageView = VulkanBackend::CreateImageView2D(backendData, target.image.image,
		format, subresourceRange);

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
