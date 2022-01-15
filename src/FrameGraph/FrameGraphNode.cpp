#include "FrameGraphNode.hpp"

FrameGraphNode::FrameGraphNode()
{

}

FrameGraphNode::~FrameGraphNode()
{

}

void FrameGraphNode::SetName(const std::string& name)
{
	this->name = name;
}

const std::string& FrameGraphNode::GetName() const
{
	return name;
}

void FrameGraphNode::UpdatePreallocatedUniformData(const std::string& name, int frameInFlight, void* data, int dataSize)
{
	uniformDescriptorSystem.UpdatePreallocated(name, frameInFlight, data, dataSize);
}

void FrameGraphNode::UpdateTexture(const std::string& name, int frameInFlight, HawkEye::HTexture texture)
{
	uniformDescriptorSystem.UpdateTexture(name, frameInFlight, texture);
}

void FrameGraphNode::UpdateStorageBuffer(const std::string& name, int frameInFlight, HawkEye::HBuffer storageBuffer)
{
	uniformDescriptorSystem.UpdateBuffer(name, frameInFlight, storageBuffer);
}

NodeOutputs* FrameGraphNode::GetOutputs()
{
	return &nodeOutputs;
}

const std::vector<InputTargetCharacteristics>& FrameGraphNode::GetInputCharacteristics() const
{
	return nodeInputCharacteristics;
}

const OutputTargetCharacteristics& FrameGraphNode::GetOutputCharacteristics()
{
	return nodeOutputCharacteristics;
}

HawkEye::HMaterial FrameGraphNode::CreateMaterial(void* data, int dataSize)
{
	// TODO: Checks (e.g., dataSize)

	int materialIndex = materialDescriptorSystems.size();
	materialDescriptorSystems.push_back(std::make_unique<DescriptorSystem>());
	materialDescriptorSystems[materialIndex]->Init(backendData, rendererData, materialData, framesInFlightCount,
		materialDescriptorSetLayout);

	int offset = 0;
	for (int u = 0; u < materialData.size(); ++u)
	{
		for (int f = 0; f < framesInFlightCount; ++f)
		{
			void* currentData = (void*)((int*)data + offset);
			if (materialData[u].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				materialDescriptorSystems[materialIndex]->UpdatePreallocated(materialData[u].name, f,
					currentData, materialData[u].size);
			}
			else if (materialData[u].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				materialDescriptorSystems[materialIndex]->UpdateTexture(materialData[u].name, f,
					(HawkEye::HTexture)currentData);
			}
			else if (materialData[u].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
			{
				materialDescriptorSystems[materialIndex]->UpdateBuffer(materialData[u].name, f,
					(HawkEye::HBuffer)currentData);
			}
		}
		offset += materialData[u].size;
	}

	drawBuffers.emplace_back();
	
	return (HawkEye::HMaterial)materialIndex;
}

void FrameGraphNode::UseBuffers(HawkEye::Pipeline::DrawBuffer* drawBuffers, int bufferCount)
{
	// TODO: Does this copy the data? Correct ones?
	this->drawBuffers[(int)drawBuffers->material] = std::vector<HawkEye::Pipeline::DrawBuffer>(drawBuffers, drawBuffers + bufferCount);
}
