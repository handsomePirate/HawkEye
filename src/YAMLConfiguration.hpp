#pragma once
#include <yaml-cpp/yaml.h>
#include <vector>
#include <string>
#include <vulkan/vulkan.hpp>

struct PipelinePass
{
	int dimension;

	enum class Type
	{
		Undefined,
		Computed,
		Rasterized
	} type = Type::Undefined;

	enum class PostProcess
	{
		Blur
	};
	std::vector<PostProcess> postProcess;

	enum class Shader
	{
		Vertex,
		Fragment,
		Compute
	};
	std::vector<std::pair<Shader, std::string>> shaders;

	enum class Target
	{
		Color,
		Depth,
		Sample
	};

	std::vector<Target> targets;

	int samples = 0;

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

	// TODO: The int for size might not be required.
	struct UniformData
	{
		std::string name;
		int size;
		VkDescriptorType type;
		VkShaderStageFlags visibility;
	};
	std::vector<UniformData> material;
	std::vector<UniformData> uniforms;
};

PipelinePass ConfigureLayer(const YAML::Node& passNode);
