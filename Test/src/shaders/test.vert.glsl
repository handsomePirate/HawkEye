#version 450
#extension GL_ARB_separate_shader_objects : enable

// per vertex
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

// per instance
layout(location = 3) in vec4 matRow0;
layout(location = 4) in vec4 matRow1;
layout(location = 5) in vec4 matRow2;
layout(location = 6) in vec4 matRow3;

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
	mat4 modelMatrix;
	modelMatrix[0] = matRow0;
	modelMatrix[1] = matRow1;
	modelMatrix[2] = matRow2;
	modelMatrix[3] = matRow3;
	modelMatrix = transpose(modelMatrix);

	gl_Position = transform.matrix * modelMatrix * vec4(inPosition, 1.0f);
	outPosition = gl_Position.xyz;
	outNormal = inNormal;
	outUv = inUv;
}
