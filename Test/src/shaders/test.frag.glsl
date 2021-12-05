#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0) uniform SimpleInt
{
	int param;
} simpleInt;

layout(set = 0, binding = 1) uniform Complex
{
	int param1;
	int param2;
} complex;

layout(set = 0, binding = 2) uniform sampler2D testTexture;

void main()
{
	outFragColor = texture(testTexture, uv);
	//outFragColor = complex.param1 + complex.param2 == simpleInt.param ? vec4(color, 1) : vec4(0, 1, 0, 1);
}