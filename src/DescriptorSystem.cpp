#include "DescriptorSystem.hpp"
#include "Resources.hpp"

DescriptorSystem::~DescriptorSystem()
{
	if (descriptorPool != VK_NULL_HANDLE)
	{
		VulkanBackend::DestroyDescriptorPool(*backendData, descriptorPool);
	}

	for (auto& preallocatedBuffer : preallocatedBuffers)
	{
		HawkEye::DeleteBuffer(rendererData, preallocatedBuffer.second);
	}
}

VkDescriptorSetLayout DescriptorSystem::InitSetLayout(VulkanBackend::BackendData* backendData,
	const std::vector<UniformData>& uniformData)
{
	// descriptor set layout
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings(uniformData.size());
	for (int b = 0; b < layoutBindings.size(); ++b)
	{
		layoutBindings[b].binding = b;
		layoutBindings[b].descriptorCount = 1;
		layoutBindings[b].descriptorType = uniformData[b].type;
		layoutBindings[b].stageFlags = uniformData[b].visibility;
	}

	return VulkanBackend::CreateDescriptorSetLayout(*backendData, layoutBindings);
}

std::string GetFramedName(const std::string& name, int frameInFlight)
{
	return std::to_string(frameInFlight) + '_' + name;
}

void DescriptorSystem::Init(VulkanBackend::BackendData* backendData, HawkEye::HRendererData rendererData,
	const std::vector<UniformData>& uniformData, int framesInFlightCount, VkDescriptorSetLayout descriptorSetLayout)
{
	descriptorSets.resize(framesInFlightCount);
	if (uniformData.empty())
	{
		return;
	}

	this->backendData = backendData;
	this->rendererData = rendererData;

	// descriptor pool sizes
	std::vector<VkDescriptorPoolSize> poolSizes(3);
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	int cumulativeSize = 0;
	for (int u = 0; u < uniformData.size(); ++u)
	{
		if (uniformData[u].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		{
			for (int f = 0; f < framesInFlightCount; ++f)
			{
				preallocatedBuffers[GetFramedName(uniformData[u].name, f)] = HawkEye::UploadBuffer(rendererData,
					nullptr, uniformData[u].size, HawkEye::BufferUsage::Uniform,
					uniformData[u].deviceLocal ? HawkEye::BufferType::DeviceLocal : HawkEye::BufferType::Mapped);
			}

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

	std::vector<VkDescriptorPoolSize> filteredPoolSizes;
	for (int s = 0; s < poolSizes.size(); ++s)
	{
		if (poolSizes[s].descriptorCount > 0)
		{
			filteredPoolSizes.push_back(poolSizes[s]);
		}
	}

	// descriptor pool
	descriptorPool = VulkanBackend::CreateDescriptorPool(*backendData, filteredPoolSizes, framesInFlightCount);

	// descriptor sets
	for (int f = 0; f < framesInFlightCount; ++f)
	{
		descriptorSets[f] = VulkanBackend::AllocateDescriptorSet(*backendData, descriptorPool, descriptorSetLayout);
	}

	// write
	int k = 0;
	cumulativeSize = 0;
	while (k < uniformData.size())
	{
		resourceBindings[uniformData[k].name] = k;
		if (uniformData[k].type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		{
			++k;
			continue;
		}

		// TODO: Make more efficient.
		int bufferInfosSize = 0;
		for (int f = 0; f < framesInFlightCount; ++f)
		{
			std::vector<VkDescriptorBufferInfo> bufferInfos;

			int u = k;
			for (; u < uniformData.size() &&
				uniformData[u].visibility == uniformData[k].visibility &&
				uniformData[u].type == uniformData[k].type;
				++u)
			{
				VkDescriptorBufferInfo bufferInfo{};
				bufferInfo.buffer = preallocatedBuffers.at(GetFramedName(uniformData[u].name, f))->buffer.buffer;
				bufferInfo.offset = 0;
				bufferInfo.range = (VkDeviceSize)uniformData[u].size;

				bufferInfos.push_back(bufferInfo);
				cumulativeSize += uniformData[u].size;
			}

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstBinding = k;
			descriptorWrite.dstSet = descriptorSets[f];
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = uniformData[k].type;
			descriptorWrite.descriptorCount = (uint32_t)bufferInfos.size();
			descriptorWrite.pBufferInfo = bufferInfos.data();

			vkUpdateDescriptorSets(backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);

			bufferInfosSize = (int)bufferInfos.size();
		}
		k += bufferInfosSize;
	}
}

VkDescriptorSet DescriptorSystem::GetSet(int frameInFlight) const
{
	return descriptorSets[frameInFlight];
}

void DescriptorSystem::UpdatePreallocated(const std::string& name, int frameInFlight, void* data, int dataSize)
{
	// TODO: Check existence?
	HawkEye::UpdateBuffer(rendererData, preallocatedBuffers[GetFramedName(name, frameInFlight)], data, dataSize);
}

void DescriptorSystem::UpdateBuffer(const std::string& name, int frameInFlight, HawkEye::HBuffer buffer)
{
	// TODO: Check existence?
	HawkEye::WaitForUpload(rendererData, buffer);

	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = buffer->buffer.buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = buffer->dataSize;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = descriptorSets[frameInFlight];
	descriptorWrite.dstBinding = resourceBindings[name];
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void DescriptorSystem::UpdateTexture(const std::string& name, int frameInFlight, HawkEye::HTexture texture)
{
	// TODO: Check existence?
	HawkEye::WaitForUpload(rendererData, texture);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = texture->imageLayout;
	imageInfo.imageView = texture->imageView;
	imageInfo.sampler = texture->sampler;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = descriptorSets[frameInFlight];
	descriptorWrite.dstBinding = resourceBindings[name];
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(backendData->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}
