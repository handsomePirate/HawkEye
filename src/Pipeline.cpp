#include "HawkEye/HawkEyeAPI.hpp"
#include <VulkanBackend/Logger.hpp>
#include <VulkanBackend/VulkanBackendAPI.hpp>
#include <SoftwareCore/Filesystem.hpp>
#include <yaml-cpp/yaml.h>
#include <regex>

void HawkEye::Pipeline::Configure(RendererData rendererData, const char* configFile,
	void* windowHandle, void* connection)
{
	VulkanBackend::Initialized* backendData = (VulkanBackend::Initialized*)rendererData;
	
	YAML::Node configData = YAML::LoadFile(configFile);

	std::vector<std::string> stages;
	if (configData["Stages"])
	{
		stages.resize(configData["Stages"].size());
		for (int s = 0; s < configData["Stages"].size(); ++s)
		{
			stages[s] = configData["Stages"][s].as<std::string>();
		}
	}
	else
	{
		CoreLogError(VulkanLogger, "Pipeline: Missing stage configuration (list of stages).");
		return;
	}

	std::vector<std::string> targets;
	if (configData["Targets"])
	{
		targets.resize(configData["Targets"].size());
		for (int t = 0; t < configData["Targets"].size(); ++t)
		{
			targets[t] = configData["Targets"][t].as<std::string>();
		}
	}
	else
	{
		targets.push_back("color");
	}

	std::vector<std::string> shaders;
	if (configData["Shaders"])
	{
		shaders.resize(configData["Shaders"].size());
		if (shaders.size() < 2)
		{
			CoreLogError(VulkanLogger, "Pipeline: Too few shaders specified (at least 2 required).");
			return;
		}

		bool hasVertex = false, hasFragment = false;
		std::regex vertexMatch(".+\\.vert\\..+");
		std::regex fragmentMatch(".+\\.frag\\..+");
		for (int s = 0; s < configData["Shaders"].size(); ++s)
		{
			shaders[s] = configData["Shaders"][s].as<std::string>();
			if (std::regex_match(shaders[s], vertexMatch))
			{
				hasVertex = true;
			}
			else if (std::regex_match(shaders[s], fragmentMatch))
			{
				hasFragment = true;
			}
		}

		if (!hasVertex)
		{
			CoreLogError(VulkanLogger, "Pipeline: Shader list does not contain a vertex shader (must contain '.vert.' in the file name).");
		}
		if (!hasFragment)
		{
			CoreLogError(VulkanLogger, "Pipeline: Shader list does not contain a fragment shader (must contain '.frag.' in the file name).");
		}
		if (!hasVertex || !hasFragment)
		{
			return;
		}
	}
	else
	{
		CoreLogError(VulkanLogger, "Pipeline: Missing shader configuration (list of shaders).");
		return;
	}

	std::vector<std::string> options;
	if (configData["Options"])
	{
		options.resize(configData["Options"].size());
		for (int o = 0; o < configData["Options"].size(); ++o)
		{
			options[o] = configData["Options"][o].as<std::string>();
		}
	}

	CoreLogDebug(VulkanLogger, "Pipeline: Configuration successful.");
}

void HawkEye::Pipeline::DrawFrame(RendererData rendererData)
{

}

void HawkEye::Pipeline::Resize(RendererData rendererData)
{

}
