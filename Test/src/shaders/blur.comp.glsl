#version 450
#extension GL_ARB_separate_shader_objects : enable
#pragma optionNV(unroll all)

layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 1, rgba8) uniform writeonly image2D resultImage;
layout(set = 0, binding = 2) uniform sampler2D sourceImage;

layout(set = 1, binding = 0) uniform Transform
{
	mat3 matrix;
} transform;

void main()
{
	vec3 outColor = vec3(0);
	int weight = 0;
	const int blurBound = 5;
	for (int x = -blurBound; x <= blurBound; ++x)
	{
		for (int y = -blurBound; y <= blurBound; ++y)
		{
			vec3 pixColor = texelFetch(sourceImage, ivec2(gl_GlobalInvocationID.xy) + ivec2(x, y), 0).rgb;
			outColor += pixColor;
			weight += 1;
		}
	}
	vec4 resColor = vec4(outColor / float(weight), 1);
	imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), resColor);
}
