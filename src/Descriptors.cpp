#include "Descriptors.hpp"

/*
VkDescriptorSetLayout DescriptorUtils::GetSetLayout(const VulkanBackend::BackendData& backendData,
	const std::vector<UniformData>& uniformData)
{
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings(uniformData.size());
	for (int b = 0; b < layoutBindings.size(); ++b)
	{
		layoutBindings[b].binding = b;
		layoutBindings[b].descriptorCount = 1;
		layoutBindings[b].descriptorType = uniformData[b].type;
		layoutBindings[b].stageFlags = uniformData[b].visibility;
	}

	return VulkanBackend::CreateDescriptorSetLayout(backendData, layoutBindings);
}

std::vector<VkDescriptorPoolSize> DescriptorUtils::FilterPoolSizes(const std::vector<VkDescriptorPoolSize>& inPoolSizes)
{
	std::vector<VkDescriptorPoolSize> filteredPoolSizes;
	for (int s = 0; s < inPoolSizes.size(); ++s)
	{
		if (inPoolSizes[s].descriptorCount > 0)
		{
			filteredPoolSizes.push_back(inPoolSizes[s]);
		}
	}

	return filteredPoolSizes;
}

std::vector<VkDescriptorPoolSize> DescriptorUtils::GetPoolSizes(HawkEye::HRendererData rendererData,
	const std::vector<UniformData>& uniformData, HawkEye::BufferType uniformBufferType, std::map<std::string,
	HawkEye::HBuffer>& uniformBuffers, bool filter, const std::string& namePrepend, void* data)
{
	std::vector<VkDescriptorPoolSize> poolSizes(3);
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	int cumulativeSize = 0;
	for (int u = 0; u < uniformData.size(); ++u)
	{
		if (uniformData[u].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		{
			uniformBuffers[namePrepend + uniformData[u].name] = HawkEye::UploadBuffer(rendererData,
				data == nullptr ? data : (void*)((int*)data + (cumulativeSize >> 2)),
				uniformData[u].size, HawkEye::BufferUsage::Uniform, uniformBufferType);
			++poolSizes[0].descriptorCount;
		}
		else if (uniformData[u].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			++poolSizes[1].descriptorCount;
		}
		else if (uniformData[u].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
		{
			++poolSizes[2].descriptorCount;
		}
		cumulativeSize += uniformData[u].size;
	}

	if (filter)
	{
		return FilterPoolSizes(poolSizes);
	}
	else
	{
		return poolSizes;
	}
}

void DescriptorUtils::UpdateSets(HawkEye::HRendererData rendererData, const VulkanBackend::BackendData& backendData,
	const std::vector<UniformData>& uniformData, const std::vector<VkDescriptorPoolSize>& poolSizes,
	VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet, VkDescriptorSetLayout descriptorSetLayout,
	const std::map<std::string, HawkEye::HBuffer>& uniformBuffers, std::map<std::string, int>& uniformTextureBindings,
	std::map<std::string, int>& storageBufferBindings, const std::string& namePrepend, void* data)
{
	descriptorPool = VulkanBackend::CreateDescriptorPool(backendData, poolSizes, 1);
	descriptorSet = VulkanBackend::AllocateDescriptorSet(backendData, descriptorPool, descriptorSetLayout);

	int k = 0;
	int cumulativeSize = 0;
	while (k < uniformData.size())
	{
		if (uniformData[k].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
		{
			storageBufferBindings[namePrepend + uniformData[k].name] = k;
			++k;
			continue;
		}

		if (uniformData[k].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			if (!data)
			{
				uniformTextureBindings[namePrepend + uniformData[k].name] = k;
			}
			else
			{
				HawkEye::HTexture texture = *(HawkEye::HTexture*)((int*)data + (cumulativeSize >> 2));
				HawkEye::WaitForUpload(rendererData, texture);

				VkDescriptorImageInfo imageInfo{};
				imageInfo.imageLayout = texture->imageLayout;
				imageInfo.imageView = texture->imageView;
				imageInfo.sampler = texture->sampler;

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = descriptorSet;
				descriptorWrite.dstBinding = k;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pImageInfo = &imageInfo;

				vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
				cumulativeSize += uniformData[k].size;
			}
			++k;
			continue;
		}

		std::vector<VkDescriptorBufferInfo> bufferInfos;

		int u = k;
		for (; u < uniformData.size() &&
			uniformData[u].visibility == uniformData[k].visibility &&
			uniformData[u].type == uniformData[k].type;
			++u)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffers.at(namePrepend + uniformData[u].name)->buffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = (VkDeviceSize)uniformData[u].size;

			bufferInfos.push_back(bufferInfo);
			cumulativeSize += uniformData[u].size;
		}

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptorSet;
		descriptorWrite.dstBinding = k;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = uniformData[k].type;
		descriptorWrite.descriptorCount = (uint32_t)bufferInfos.size();
		descriptorWrite.pBufferInfo = bufferInfos.data();

		vkUpdateDescriptorSets(backendData.logicalDevice, 1, &descriptorWrite, 0, nullptr);
		k += (int32_t)bufferInfos.size();
	}
}
*/
