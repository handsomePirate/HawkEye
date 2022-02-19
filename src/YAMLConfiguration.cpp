#include "YAMLConfiguration.hpp"
#include <SoftwareCore/DefaultLogger.hpp>
#include <regex>

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

			int size = sizeof(void*);
			if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && passNode[u]["size"])
			{
				CoreLogWarn(DefaultLogger, "Pipeline uniforms: Size is irrelevant for texture uniforms - skipping.");
			}
			if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && passNode[u]["size"])
			{
				CoreLogWarn(DefaultLogger, "Pipeline uniforms: Size is irrelevant for storage buffer uniforms - skipping.");
			}
			if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				if (!passNode[u]["size"])
				{
					CoreLogWarn(DefaultLogger, "Pipeline uniforms: Missing uniform size - skipping.");
					continue;
				}
				else
				{
					size = passNode[u]["size"].as<int>();
				}
			}

			std::string name = passNode[u]["name"].as<std::string>();

			bool deviceLocal = false;
			if (passNode["residency"])
			{
				if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
				{
					std::string residency = passNode["residency"].as<std::string>();
					if (residency == "gpu")
					{
						deviceLocal = true;
					}
					else if (residency != "cpu")
					{
						CoreLogError(DefaultLogger, "Pipeline uniforms: Incorrect residency parameter - defaulting to cpu.");
					}
				}
				else
				{
					CoreLogWarn(DefaultLogger, "Pipeline uniforms: Specifying residency for descriptor of a different type than uniform - skipping.");
				}
			}

			if (name[0] == '_')
			{
				CoreLogError(DefaultLogger, "Pipeline uniforms: Name may not start with \'_\' - skipping.");
				continue;
			}
			uniformData.push_back(
				{
					name,
					size, type, visibility,
					deviceLocal
				});
		}
	}
}

std::string FrameGraphConfigurator::GetName(const YAML::Node& nodeConfiguration)
{
	if (nodeConfiguration)
	{
		// TODO: Make sure the name is unique.
		return nodeConfiguration.as<std::string>();
	}

	CoreLogFatal(DefaultLogger, "Configuration: Node name not specified.");
	return "";
}

VkFormat GetFormat(const YAML::Node& nodeConfiguration)
{
	static std::map<std::string, int> typeOffset8
	{
		{"unorm", 0},
		{"snorm", 1},
		{"uscaled", 2},
		{"sscaled", 3},
		{"uint", 4},
		{"sint", 5},
	};
	static int typeOffset8Size = (int)typeOffset8.size();
	static std::map<std::string, int> typeOffset16
	{
		{"unorm", 0},
		{"snorm", 1},
		{"uscaled", 2},
		{"sscaled", 3},
		{"uint", 4},
		{"sint", 5},
		{"sfloat", 6},
	};
	static int typeOffset16Size = (int)typeOffset16.size();
	static std::map<std::string, int> typeOffset32
	{
		{"uint", 0},
		{"sint", 1},
		{"sfloat", 2},
	};
	static int typeOffset32Size = (int)typeOffset32.size();

	static std::map<int, int> bitDepthStart
	{
		{8, (int)VK_FORMAT_R8_UNORM},
		{16, (int)VK_FORMAT_R16_UNORM},
		{32, (int)VK_FORMAT_R32_UINT},
	};

	if (nodeConfiguration)
	{
		if (!nodeConfiguration["bit-depth"])
		{
			std::string defaultString = nodeConfiguration.as<std::string>();
			if (defaultString == "default")
			{
				return VK_FORMAT_UNDEFINED;
			}
			else
			{
				CoreLogError(DefaultLogger, "Configuration: Format incorrectly specified.");
				return VK_FORMAT_UNDEFINED;
			}
		}
		int bitDepth = nodeConfiguration["bit-depth"].as<int>();
		int channelCount = nodeConfiguration["channel-count"].as<int>();
		std::string type = nodeConfiguration["type"].as<std::string>();

		auto bitDepthIt = bitDepthStart.find(bitDepth);
		if (bitDepthIt == bitDepthStart.end())
		{
			CoreLogError(DefaultLogger, "Configuration: Format incorrectly specified - \'bit-depth\' must be 8, 16, or 32.");
			return VK_FORMAT_UNDEFINED;
		}
		int formatOffset = bitDepthIt->second;
		int typeOffsetSize;
		if (bitDepth == 8)
		{
			auto typeOffsetIt = typeOffset8.find(type);
			if (typeOffsetIt == typeOffset8.end())
			{
				CoreLogError(DefaultLogger,
					"Configuration: Format incorrectly specified - \'type\' must be %s, %s, %s, %s, %s, or %s for \'bit-depth\' of 8.",
					"unorm", "snorm", "uscaled", "sscaled", "uint", "sint");
				return VK_FORMAT_UNDEFINED;
			}
			formatOffset += typeOffsetIt->second;
			typeOffsetSize = typeOffset8Size;
		}
		else if (bitDepth == 16)
		{
			auto typeOffsetIt = typeOffset16.find(type);
			if (typeOffsetIt == typeOffset16.end())
			{
				CoreLogError(DefaultLogger,
					"Configuration: Format incorrectly specified - \'type\' must be %s, %s, %s, %s, %s, %s, or %s for \'bit-depth\' of 16.",
					"unorm", "snorm", "uscaled", "sscaled", "uint", "sint", "sfloat");
				return VK_FORMAT_UNDEFINED;
			}
			formatOffset += typeOffsetIt->second;
			typeOffsetSize = typeOffset16Size;
		}
		else // bitDepth == 32
		{
			auto typeOffsetIt = typeOffset32.find(type);
			if (typeOffsetIt == typeOffset32.end())
			{
				CoreLogError(DefaultLogger,
					"Configuration: Format incorrectly specified - \'type\' must be %s, %s, or %s for \'bit-depth\' of 32.",
					"uint", "sint", "sfloat");
				return VK_FORMAT_UNDEFINED;
			}
			formatOffset += typeOffsetIt->second;
			typeOffsetSize = typeOffset32Size;
		}

		if (channelCount < 1 || channelCount > 4)
		{
			CoreLogError(DefaultLogger, "Configuration: Format incorrectly specified - \'channel-count\' must be >= 1 and <= 4.");
			return VK_FORMAT_UNDEFINED;
		}

		formatOffset += channelCount * typeOffsetSize;

		return (VkFormat)formatOffset;
	}

	CoreLogFatal(DefaultLogger, "Configuration: Format not defined.");
	return VK_FORMAT_UNDEFINED;
}

ImageFormat GetImageFormat(const YAML::Node& nodeConfiguration, TargetType type)
{
	ImageFormat imageFormat;
	if (!nodeConfiguration["format"].IsDefined())
	{
		CoreLogFatal(DefaultLogger, "Configuration: Format not defined.");
		return imageFormat;
	}

	if (nodeConfiguration["format"].IsMap())
	{
		imageFormat.format = GetFormat(nodeConfiguration["format"]);
	}
	else
	{
		std::string formatString = nodeConfiguration["format"].as<std::string>();
		if (formatString == "color-optimal")
		{
			imageFormat.metadata = ImageFormat::Metadata::ColorOptimal;
		}
		else if (formatString == "depth-optimal")
		{
			imageFormat.metadata = ImageFormat::Metadata::DepthOptimal;
		}
		else if (formatString == "optimal")
		{
			if (type == TargetType::Color)
			{
				imageFormat.metadata = ImageFormat::Metadata::ColorOptimal;
			}
			else if (type == TargetType::Depth)
			{
				imageFormat.metadata = ImageFormat::Metadata::DepthOptimal;
			}
		}
	}

	return imageFormat;
}

std::unique_ptr<OutputImageCharacteristics> GetOutputImageCharacteristics(const YAML::Node& nodeConfiguration, TargetType type)
{
	// width modifier
	float widthModifier = 1.f;
	if (nodeConfiguration["width-modifier"])
	{
		widthModifier = nodeConfiguration["width-modifier"].as<float>();
	}
	// height modifier
	float heightModifier = 1.f;
	if (nodeConfiguration["height-modifier"])
	{
		heightModifier = nodeConfiguration["height-modifier"].as<float>();
	}
	// format
	ImageFormat imageFormat = GetImageFormat(nodeConfiguration, type);

	// read/write.
	bool read = true;
	bool write = true;
	if (nodeConfiguration["access"])
	{
		std::string access = nodeConfiguration["access"].as<std::string>();
		if (access == "r")
		{
			read = true;
			write = false;
		}
		else if (access == "w")
		{
			write = true;
			read = false;
		}
		else if (access != "rw" && access != "wr")
		{
			CoreLogFatal(DefaultLogger, "Configuration: Incorrect output access format - \'r\', \'w\', \'rw\', or \'wr\' supported.");
			return nullptr;
		}
	}
	else
	{
		CoreLogFatal(DefaultLogger, "Configuration: Node output access not specified.");
		return nullptr;
	}


	return std::make_unique<OutputImageCharacteristics>(
		OutputImageCharacteristics{ widthModifier, heightModifier, imageFormat, read, write });
}

std::unique_ptr<InputImageCharacteristics> GetInputImageCharacteristics(const YAML::Node& nodeConfiguration, TargetType type)
{
	// width modifier
	float widthModifier = 1.f;
	if (nodeConfiguration["width-modifier"])
	{
		widthModifier = nodeConfiguration["width-modifier"].as<float>();
	}
	// height modifier
	float heightModifier = 1.f;
	if (nodeConfiguration["height-modifier"])
	{
		heightModifier = nodeConfiguration["height-modifier"].as<float>();
	}
	// format
	ImageFormat imageFormat = GetImageFormat(nodeConfiguration, type);

	// connection name
	std::string connectionName = "";
	if (nodeConfiguration["connection-name"])
	{
		// TODO: Make sure this name exists in the graph.
		connectionName = nodeConfiguration["connection-name"].as<std::string>();
	}
	// connection slot
	int connectionSlot = -1;
	if (nodeConfiguration["connection-slot"])
	{
		// TODO: Make sure this slot exists in the node.
		connectionSlot = nodeConfiguration["connection-slot"].as<int>();
	}
	// content operation
	ContentOperation contentOperation = ContentOperation::DontCare;
	if (nodeConfiguration["content-operation"])
	{
		std::string contentOperationString = nodeConfiguration["content-operation"].as<std::string>();
		if (contentOperationString == "clear")
		{
			contentOperation = ContentOperation::Clear;
		}
		else if (contentOperationString == "preserve")
		{
			contentOperation = ContentOperation::Preserve;
		}
		else if (contentOperationString != "dontcare")
		{
			CoreLogError(DefaultLogger, "Configuration: Input image operation incorrectly specified - must be \'clear\', \'preserve\', or \'dontcare\'.");
		}
	}

	return std::make_unique<InputImageCharacteristics>(
		InputImageCharacteristics{ widthModifier, heightModifier, imageFormat, connectionName, connectionSlot, contentOperation });
}

std::vector<InputTargetCharacteristics> FrameGraphConfigurator::GetInputCharacteristics(const YAML::Node& nodeConfiguration)
{
	std::vector<InputTargetCharacteristics> result;
	if (nodeConfiguration)
	{
		for (int c = 0; c < nodeConfiguration.size(); ++c)
		{
			InputTargetCharacteristics targetCharacteristics{};
			if (nodeConfiguration[c]["color"])
			{
				targetCharacteristics.colorTarget = GetInputImageCharacteristics(nodeConfiguration[c]["color"], TargetType::Color);
			}
			if (nodeConfiguration[c]["depth"])
			{
				// TODO: Have default format?
				targetCharacteristics.depthTarget = GetInputImageCharacteristics(nodeConfiguration[c]["depth"], TargetType::Depth);
			}
			if (nodeConfiguration[c]["sample"])
			{
				targetCharacteristics.sampleTarget = GetInputImageCharacteristics(nodeConfiguration[c]["sample"], TargetType::Color);
			}
			result.push_back(std::move(targetCharacteristics));
		}
		return std::move(result);
	}

	CoreLogFatal(DefaultLogger, "Configuration: Node inputs not specified.");
	return {};
}

OutputTargetCharacteristics FrameGraphConfigurator::GetOutputCharacteristics(const YAML::Node& nodeConfiguration)
{
	OutputTargetCharacteristics result{};
	if (nodeConfiguration)
	{
		if (nodeConfiguration["color"])
		{
			result.colorTarget = GetOutputImageCharacteristics(nodeConfiguration["color"], TargetType::Color);
		}
		if (nodeConfiguration["depth"])
		{
			// TODO: Have default format?
			result.depthTarget = GetOutputImageCharacteristics(nodeConfiguration["depth"], TargetType::Depth);
		}
		if (nodeConfiguration["sample"])
		{
			result.sampleTarget = GetOutputImageCharacteristics(nodeConfiguration["sample"], TargetType::Color);
		}
		return result;
	}

	CoreLogFatal(DefaultLogger, "Configuration: Node output not specified.");
	return result;
}

std::vector<VertexAttribute> FrameGraphConfigurator::GetVertexAttributes(const YAML::Node& nodeConfiguration)
{
	std::vector<VertexAttribute> result;
	if (nodeConfiguration)
	{
		for (int a = 0; a < nodeConfiguration.size(); ++a)
		{
			std::string attributeString = nodeConfiguration[a].as<std::string>();
			int byteCount;
			VertexAttribute::Type type;
			std::regex vecRegex("vec[2-4]");
			std::regex ivecRegex("ivec[2-4]");
			std::regex uvecRegex("uvec[2-4]");
			bool isWellFormed = false;
			bool isVector = false;
			if (std::regex_match(attributeString, vecRegex) || attributeString == "float")
			{
				type = VertexAttribute::Type::Float;
				isWellFormed = true;
				isVector = attributeString != "float";
			}
			else if (std::regex_match(attributeString, ivecRegex) || attributeString == "int")
			{
				type = VertexAttribute::Type::Int;
				isWellFormed = true;
				isVector = attributeString != "int";
			}
			else if (std::regex_match(attributeString, uvecRegex) || attributeString == "uint")
			{
				type = VertexAttribute::Type::Uint;
				isWellFormed = true;
				isVector = attributeString != "uint";
			}

			if (!isWellFormed)
			{
				CoreLogWarn(DefaultLogger, "Pipeline pass: Vertex attributes not well formed (can be [ui]?vec[2-4]).");
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

			result.push_back({ byteCount, type });
		}
		return result;
	}

	CoreLogFatal(DefaultLogger, "Configuration: Vertex attributes not specified.");
	return result;
}

std::vector<std::pair<Shader, std::string>> FrameGraphConfigurator::GetShaders(const YAML::Node& nodeConfiguration)
{
	std::vector<std::pair<Shader, std::string>> result;
	if (nodeConfiguration)
	{
		if (nodeConfiguration["vertex"])
		{
			result.emplace_back(Shader::Vertex, nodeConfiguration["vertex"].as<std::string>());
		}
		if (nodeConfiguration["fragment"])
		{
			result.emplace_back(Shader::Fragment, nodeConfiguration["fragment"].as<std::string>());
		}
		if (nodeConfiguration["compute"])
		{
			result.emplace_back(Shader::Compute, nodeConfiguration["compute"].as<std::string>());
		}
		return result;
	}

	CoreLogFatal(DefaultLogger, "Configuration: Shaders not specified.");
	return result;
}

VkCullModeFlags FrameGraphConfigurator::GetCullMode(const YAML::Node& nodeConfiguration)
{
	if (nodeConfiguration)
	{
		std::string cullMode = nodeConfiguration.as<std::string>();
		if (cullMode == "back")
		{
			return VK_CULL_MODE_BACK_BIT;
		}
		else if (cullMode == "front")
		{
			return VK_CULL_MODE_FRONT_BIT;
		}
		else
		{
			if (cullMode != "none")
			{
				CoreLogError(DefaultLogger, "Pipeline pass: Wrong cull mode format - none assumed.");
			}
			return VK_CULL_MODE_NONE;
		}
	}
	
	return VK_CULL_MODE_NONE;
}
