#pragma once
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
};

enum class PipelineTarget
{
	Color,
	Depth,
	Sample
};

struct PipelineUniforms
{
	std::vector<UniformData> uniforms;
};

struct PipelinePass
{
	int dimension;
	bool inheritDepth = false;

	enum class Type
	{
		Undefined,
		Computed,
		Rasterized
	} type = Type::Undefined;

	enum class Shader
	{
		Vertex,
		Fragment,
		Compute
	};
	std::vector<std::pair<Shader, std::string>> shaders;

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
	std::vector<VertexAttribute> attributes;
	VkCullModeFlags cullMode;

	std::vector<UniformData> material;
	std::vector<UniformData> uniforms;
};

PipelinePass ConfigureLayer(const YAML::Node& passNode);
void ConfigureCommon(const YAML::Node& passNode, std::vector<PipelineTarget>& pipelineTargets, int& samples);
void ConfigureUniforms(const YAML::Node& passNode, std::vector<UniformData>& uniformData);
