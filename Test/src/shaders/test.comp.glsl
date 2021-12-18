#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba8) uniform writeonly image2D resultImage;

void main()
{
	imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), vec4(vec3(gl_GlobalInvocationID.xy, 1) / vec3(gl_NumWorkGroups * gl_WorkGroupSize), 1));
}
