#include "YAMLConfiguration.hpp"
#include <VulkanBackend/Logger.hpp>
#include <regex>

PipelinePass ConfigureLayer(const YAML::Node& passNode)
{
	PipelinePass pass{};

	if (!passNode["type"])
	{
		CoreLogError(VulkanLogger, "Pipeline pass: type was not defined.");
		return pass;
	}

	if (!passNode["dim"])
	{
		CoreLogError(VulkanLogger, "Pipeline pass: dimension not specified (provide dim).");
		return pass;
	}
	else
	{
		pass.dimension = passNode["dim"].as<int>();
		if (pass.dimension != 2 && pass.dimension != 3)
		{
			CoreLogError(VulkanLogger, "Pipeline pass: wrong dimension parameter - must be 2 or 3.");
			return pass;
		}
	}

	if (passNode["inherit"])
	{
		if (passNode["inherit"]["depth"])
		{
			pass.inheritDepth = passNode["inherit"]["depth"].as<bool>();
		}
	}

	std::string type = passNode["type"].as<std::string>();

	if (type == "computed")
	{
		pass.type = PipelinePass::Type::Computed;
	}
	else if (type == "rasterized")
	{
		pass.type = PipelinePass::Type::Rasterized;
	}
	else
	{
		CoreLogError(VulkanLogger, "Pipeline pass: Undefined pass type - %s.", type);
		return pass;
	}

	if (passNode["shaders"])
	{
		if (pass.type == PipelinePass::Type::Rasterized)
		{
			// TODO: Make sure there is two shaders - one vertex and one fragment.
			if (passNode["shaders"]["vertex"])
			{
				pass.shaders.emplace_back(PipelinePass::Shader::Vertex, passNode["shaders"]["vertex"].as<std::string>());
			}
			else
			{
				CoreLogError(VulkanLogger, "Pipeline pass: Vertex shader missing in a rasterized pass.");
			}
			if (passNode["shaders"]["fragment"])
			{
				pass.shaders.emplace_back(PipelinePass::Shader::Fragment, passNode["shaders"]["fragment"].as<std::string>());
			}
			else
			{
				CoreLogError(VulkanLogger, "Pipeline pass: Fragment shader missing in a rasterized pass.");
			}
		}
		else
		{
			// TODO: Make sure there is only one shader.
			if (passNode["shaders"]["compute"])
			{
				pass.shaders.emplace_back(PipelinePass::Shader::Compute, passNode["shaders"]["compute"].as<std::string>());
			}
			else
			{
				CoreLogError(VulkanLogger,
					"Pipeline pass: A computed pass should have a compute shader, other shader types ignored.");
			}
		}
	}

	if (passNode["vertex attributes"])
	{
		for (int a = 0; a < passNode["vertex attributes"].size(); ++a)
		{
			std::string attributeString = passNode["vertex attributes"][a].as<std::string>();
			int byteCount;
			PipelinePass::VertexAttribute::Type type;
			std::regex vecRegex("vec[2-4]");
			std::regex ivecRegex("ivec[2-4]");
			std::regex uvecRegex("uvec[2-4]");
			bool isWellFormed = false;
			bool isVector = false;
			if (std::regex_match(attributeString, vecRegex) || attributeString == "float")
			{
				type = PipelinePass::VertexAttribute::Type::Float;
				isWellFormed = true;
				isVector = attributeString != "float";
			}
			else if (std::regex_match(attributeString, ivecRegex) || attributeString == "int")
			{
				type = PipelinePass::VertexAttribute::Type::Int;
				isWellFormed = true;
				isVector = attributeString != "int";
			}
			else if (std::regex_match(attributeString, uvecRegex) || attributeString == "uint")
			{
				type = PipelinePass::VertexAttribute::Type::Uint;
				isWellFormed = true;
				isVector = attributeString != "uint";
			}

			if (!isWellFormed)
			{
				CoreLogWarn(VulkanLogger, "Pipeline pass: Vertex attributes not well formed (can be [ui]?vec[2-4]).");
				continue;
			}

			if (isVector)
			{
				std::string numberString = &attributeString[attributeString.length() - 1];
				byteCount = std::stoi(numberString) * 4;
			}
			else
			{
				byteCount = 4;
			}

			pass.attributes.push_back({ byteCount, type });
		}
	}
	else
	{
		if (pass.type == PipelinePass::Type::Rasterized)
		{
			CoreLogWarn(VulkanLogger, "Pipeline pass: Vertex attributes not defined, vec3 assumed (position).");
			pass.attributes.push_back({ 12, PipelinePass::VertexAttribute::Type::Float });
		}
	}

	if (passNode["cull"])
	{
		std::string cullMode = passNode["cull"].as<std::string>();
		if (cullMode == "back")
		{
			pass.cullMode = VK_CULL_MODE_BACK_BIT;
		}
		else if (cullMode == "front")
		{
			pass.cullMode = VK_CULL_MODE_FRONT_BIT;
		}
		else
		{
			pass.cullMode = VK_CULL_MODE_NONE;
			CoreLogWarn(VulkanLogger, "Pipeline pass: Wrong cull mode format - no culling used.");
		}
	}
	else
	{
		pass.cullMode = VK_CULL_MODE_NONE;
	}

	ConfigureUniforms(passNode["material"], pass.material);
	PipelineUniforms data;
	if (pass.dimension == 3)
	{
		pass.uniforms.push_back(
			{
				"camera", (/*bytes*/4 * /*width*/4 * /*height*/4),
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL
			});
	}
	else // pass.dimension == 2
	{
		pass.uniforms.push_back(
		{
			"camera", (/*bytes*/4 * /*width*/3 * /*height*/3),
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL
		});
	}
	ConfigureUniforms(passNode["uniforms"], pass.uniforms);

	return pass;
}

void ConfigureCommon(const YAML::Node& passNode, std::vector<PipelineTarget>& pipelineTargets, int& samples)
{
	if (passNode["samples"])
	{
		samples = passNode["samples"].as<int>();
	}
	else
	{
		CoreLogWarn(VulkanLogger, "Pipeline: Sample count not defined, 1 assumed.");
		samples = 1;
	}

	if (passNode["targets"])
	{
		for (int p = 0; p < passNode["targets"].size(); ++p)
		{
			std::string targets = passNode["targets"][p].as<std::string>();

			if (targets == "color")
			{
				pipelineTargets.push_back(PipelineTarget::Color);
			}
			else if (targets == "depth")
			{
				pipelineTargets.push_back(PipelineTarget::Depth);
			}
			else if (targets == "sample")
			{
				pipelineTargets.push_back(PipelineTarget::Sample);
			}
			else
			{
				CoreLogWarn(VulkanLogger, "Pipeline pass: Undefined target type - %s.", targets);
			}
		}
	}
	else
	{
		CoreLogWarn(VulkanLogger, "Pipeline pass: No targets defined, color assumed.");
		pipelineTargets.push_back(PipelineTarget::Color);
	}
}

void ConfigureUniforms(const YAML::Node& passNode, std::vector<UniformData>& uniformData)
{
	if (passNode)
	{
		std::regex bRegex("[0-9]+");
		for (int u = 0; u < passNode.size(); ++u)
		{
			VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			if (passNode[u]["type"])
			{
				std::string typeStr = passNode[u]["type"].as<std::string>();
				if (typeStr != "uniform")
				{
					if (typeStr == "texture")
					{
						type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					}
					else if (typeStr == "storage")
					{
						type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					}
				}
			}

			VkShaderStageFlags visibility = VK_SHADER_STAGE_ALL;
			if (passNode[u]["visibility"])
			{
				visibility = 0;
				std::vector<std::string> visibilityArr;
				std::string visibilityStr = passNode[u]["visibility"].as<std::string>();
				int lastDel = 0;
				int currDel = (int)visibilityStr.find('|');
				bool hadSplit = false;
				// TODO: Whitespaces.
				while (currDel != -1)
				{
					visibilityArr.push_back(visibilityStr.substr(lastDel, currDel - lastDel));
					lastDel = currDel + 1;
					currDel = (int)visibilityStr.find('|', lastDel);
					hadSplit = true;
				}
				visibilityArr.push_back(visibilityStr.substr(lastDel));

				for (int s = 0; s < visibilityArr.size(); ++s)
				{
					if (visibilityArr[s] == "vertex")
					{
						visibility |= VK_SHADER_STAGE_VERTEX_BIT;
					}
					else if (visibilityArr[s] == "fragment")
					{
						visibility |= VK_SHADER_STAGE_FRAGMENT_BIT;
					}
					else if (visibilityArr[s] == "geometry")
					{
						visibility |= VK_SHADER_STAGE_GEOMETRY_BIT;
					}
					else if (visibilityArr[s] == "tesselation control")
					{
						visibility |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
					}
					else if (visibilityArr[s] == "tesslation evaluation")
					{
						visibility |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
					}
					else if (visibilityArr[s] == "compute")
					{
						visibility |= VK_SHADER_STAGE_COMPUTE_BIT;
					}
					else if (visibilityArr[s] == "graphics")
					{
						visibility |= VK_SHADER_STAGE_ALL_GRAPHICS;
					}
					else if (visibilityArr[s] == "all")
					{
						visibility |= VK_SHADER_STAGE_ALL;
					}
				}
			}

			int size = 0;
			if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && passNode[u]["size"])
			{
				CoreLogWarn(VulkanLogger, "Pipeline uniforms: Size is irrelevant for texture uniforms - skipping.");
			}
			if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && passNode[u]["size"])
			{
				CoreLogWarn(VulkanLogger, "Pipeline uniforms: Size is irrelevant for storage buffer uniforms - skipping.");
			}
			if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				if (!passNode[u]["size"])
				{
					CoreLogError(VulkanLogger, "Pipeline uniforms: Missing uniform size - skipping.");
					continue;
				}
				else
				{
					size = passNode[u]["size"].as<int>();
				}
			}

			std::string name = passNode[u]["name"].as<std::string>();

			if (name[0] == '_')
			{
				CoreLogError(VulkanLogger, "Pipeline uniforms: Name may not start with \'_\' - skipping.");
				continue;
			}
			uniformData.push_back(
				{
					name,
					size, type, visibility
				});
		}
	}
}
