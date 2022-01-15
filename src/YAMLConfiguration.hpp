#pragma once
#include "FrameGraph/NodeStructs.hpp"
#include <yaml-cpp/yaml.h>
#include <vector>
#include <string>
#include <vulkan/vulkan.hpp>

struct UniformData
{
	std::string name;
	int size;
	VkDescriptorType type;
	VkShaderStageFlags visibility;
	bool deviceLocal;
};

struct VertexAttribute
{
	int byteCount;
	enum class Type
	{
		Uint = 0,
		Int = 1,
		Float = 2
	} type;
};

enum class Shader
{
	Vertex,
	Fragment,
	Compute
};

void ConfigureUniforms(const YAML::Node& passNode, std::vector<UniformData>& uniformData);

namespace FrameGraphConfigurator
{
	std::string GetName(const YAML::Node& nodeConfiguration);
	std::vector<InputTargetCharacteristics> GetInputCharacteristics(const YAML::Node& nodeConfiguration);
	OutputTargetCharacteristics GetOutputCharacteristics(const YAML::Node& nodeConfiguration);
	std::vector<VertexAttribute> GetVertexAttributes(const YAML::Node& nodeConfiguration);
	std::vector<std::pair<Shader, std::string>> GetShaders(const YAML::Node& nodeConfiguration);
	VkCullModeFlags GetCullMode(const YAML::Node& nodeConfiguration);
}
