#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba8) uniform writeonly image2D resultImage;
layout(set = 0, binding = 1) uniform sampler2D sourceImage;

layout(set = 1, binding = 0) uniform Transform
{
	mat3 matrix;
} transform;

void main()
{
	vec3 pixColor = texelFetch(sourceImage, ivec2(gl_GlobalInvocationID.xy), 0).rgb;
	vec4 resColor = vec4(vec3(1) - pixColor, 1);
	imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), resColor);
}
