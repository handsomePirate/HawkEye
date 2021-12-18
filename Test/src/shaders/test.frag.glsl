#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outFragColor;

//layout(set = 0, binding = 0) uniform sampler2D textureSampler;
layout(set = 0, binding = 0) uniform Color
{
	vec3 data;
} color;

void main()
{
	outFragColor = vec4(color.data, 1);
	//outFragColor = vec4(texture(textureSampler, uv).rgb, 1);
	//outFragColor = vec4(abs(uv.s), 0, 0, 1);
	//outFragColor = vec4(1, 0, 0, 1);
}