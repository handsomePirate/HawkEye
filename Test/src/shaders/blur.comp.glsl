#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba8) uniform writeonly image2D resultImage;
layout(set = 0, binding = 1) uniform sampler2D sourceImage;

layout(set = 1, binding = 0) uniform Transform
{
	mat4 matrix;
} transform;

void main()
{
	vec3 outColor = vec3(0);
	int weight = 0;
	const int blurBound = 3;
	for (int x = -blurBound; x <= blurBound; ++x)
	{
		for (int y = -blurBound; y <= blurBound; ++y)
		{
			vec2 uv = (vec3(gl_GlobalInvocationID).xy + vec2(x, y)) / vec3(gl_NumWorkGroups * gl_WorkGroupSize).xy;
			vec3 pixColor = texture(sourceImage, uv).rgb;
			outColor += pixColor;
			weight += 1;
		}
	}
	vec4 resColor = vec4(outColor / float(weight), 1);
	imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), resColor);
}
