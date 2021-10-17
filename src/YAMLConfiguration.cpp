#include "YAMLConfiguration.hpp"
#include <VulkanBackend/Logger.hpp>

PipelineLayer ConfigureLayer(const YAML::Node& layerNode)
{
	PipelineLayer layer{};

	if (!layerNode["type"])
	{
		CoreLogError(VulkanLogger, "Pipeline layer: type was not defined.");
		return layer;
	}

	std::string type = layerNode["type"].as<std::string>();

	if (type == "computed")
	{
		layer.type = PipelineLayer::Type::Computed;
	}
	else if (type == "rasterized")
	{
		layer.type = PipelineLayer::Type::Rasterized;
	}
	else
	{
		CoreLogError(VulkanLogger, "Pipeline layer: Undefined layer type - %s.", type);
		return layer;
	}

	if (layerNode["post"])
	{
		for (int p = 0; p < layerNode["post"].size(); ++p)
		{
			std::string postProcess = layerNode["post"][p].as<std::string>();

			if (postProcess == "blur")
			{
				layer.postProcess.push_back(PipelineLayer::PostProcess::Blur);
			}
			else
			{
				CoreLogWarn(VulkanLogger, "Pipeline layer: Undefined post processing effect type - %s.", postProcess);
			}
		}
	}

	if (layerNode["targets"])
	{
		for (int p = 0; p < layerNode["targets"].size(); ++p)
		{
			std::string targets = layerNode["targets"][p].as<std::string>();

			if (targets == "color")
			{
				layer.targets.push_back(PipelineLayer::Target::Color);
			}
			else if (targets == "depth")
			{
				layer.targets.push_back(PipelineLayer::Target::Depth);
			}
			else if (targets == "sample")
			{
				layer.targets.push_back(PipelineLayer::Target::Sample);
			}
			else
			{
				CoreLogWarn(VulkanLogger, "Pipeline layer: Undefined target type - %s.", targets);
			}
		}
	}
	else
	{
		CoreLogWarn(VulkanLogger, "Pipeline layer: No targets defined, color assumed.");
		layer.targets.push_back(PipelineLayer::Target::Color);
	}

	if (layerNode["samples"])
	{
		layer.samples = layerNode["samples"].as<int>();
	}
	else
	{
		CoreLogWarn(VulkanLogger, "Pipeline layer: Sample count not defined, 1 assumed.");
		layer.samples = 1;
	}

	return layer;
}
