#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUv;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	outPosition = inPosition;
	outColor = inColor;
	outUv = inUv;
	gl_Position = vec4(outPosition, 1.0f);
}
