#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;

layout(set = 1, binding = 0) uniform Transform
{
	mat4 matrix;
} transform;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	gl_Position = vec4(inPosition, 1.0f) * transform.matrix;
	outPosition = gl_Position.xyz;
	outNormal = inNormal;
	outUv = inUv;
}
