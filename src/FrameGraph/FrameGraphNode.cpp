#include "FrameGraphNode.hpp"

FrameGraphNode::FrameGraphNode(const std::string& name, int framesInFlightCount, FrameGraphNodeType type, bool isFinal)
	: name(name), framesInFlightCount(framesInFlightCount), type(type), isFinal(isFinal) {}

FrameGraphNode::~FrameGraphNode()
{

}

const std::string& FrameGraphNode::GetName() const
{
	return name;
}

FrameGraphNodeType FrameGraphNode::GetType() const
{
	return type;
}

bool FrameGraphNode::IsFinal() const
{
	return isFinal;
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
	for (int m = 0; m < this->drawBuffers.size(); ++m)
	{
		this->drawBuffers[m].clear();
	}
	for (int b = 0; b < bufferCount; ++b)
	{
		this->drawBuffers[(int)drawBuffers[b].material].push_back(drawBuffers[b]);
	}
}
