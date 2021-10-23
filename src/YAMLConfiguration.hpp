#pragma once
#include <yaml-cpp/yaml.h>
#include <vector>
#include <string>

struct PipelineLayer
{
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
};

PipelineLayer ConfigureLayer(const YAML::Node& layerNode);
