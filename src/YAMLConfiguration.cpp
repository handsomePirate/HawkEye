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
		if (passNode["shaders"]["vertex"])
		{
			pass.shaders.emplace_back(PipelinePass::Shader::Vertex, passNode["shaders"]["vertex"].as<std::string>());
		}
		if (passNode["shaders"]["fragment"])
		{
			pass.shaders.emplace_back(PipelinePass::Shader::Fragment, passNode["shaders"]["fragment"].as<std::string>());
		}
		if (passNode["shaders"]["compute"])
		{
			pass.shaders.emplace_back(PipelinePass::Shader::Compute, passNode["shaders"]["compute"].as<std::string>());
		}
	}


	if (passNode["targets"])
	{
		for (int p = 0; p < passNode["targets"].size(); ++p)
		{
			std::string targets = passNode["targets"][p].as<std::string>();

			if (targets == "color")
			{
				pass.targets.push_back(PipelinePass::Target::Color);
			}
			else if (targets == "depth")
			{
				pass.targets.push_back(PipelinePass::Target::Depth);
			}
			else if (targets == "sample")
			{
				pass.targets.push_back(PipelinePass::Target::Sample);
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
		pass.targets.push_back(PipelinePass::Target::Color);
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
		CoreLogWarn(VulkanLogger, "Pipeline pass: Vertex attributes not defined, vec3 assumed (position).");
		pass.attributes.push_back({ 12, PipelinePass::VertexAttribute::Type::Float });
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
			if (type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				if (!passNode[u]["size"])
				{
					CoreLogError(VulkanLogger, "Pipeline uniforms: Missing uniform size - skipping uniform.");
					continue;
				}
				else
				{
					size = passNode[u]["size"].as<int>();
				}
			}

			uniformData.push_back(
				{
					passNode[u]["name"].as<std::string>(),
					size, type, visibility
				});
		}
	}
}
