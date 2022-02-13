#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba8) uniform writeonly image2D resultImage;

const int maxIteration = 100;

void main()
{
	float a = 0;
	float a_buf;
	float b = 0;

	float x = (gl_GlobalInvocationID.x / float(gl_NumWorkGroups.x * gl_WorkGroupSize.x) - 0.75f) * 2.5f;
	float y = (gl_GlobalInvocationID.y / float(gl_NumWorkGroups.y * gl_WorkGroupSize.y) - .5f) * 2.5f;

	int n = 0;
	for (n; n < maxIteration; ++n)
	{
		if (a * a + b * b > 4.f)
		{
			break;
		}

		a_buf = a;
		a = a_buf * a_buf - b * b + x;
		b = 2 * a_buf * b + y;
	}
	
	vec4 color;
	if(n < maxIteration)
	{
		color = vec4(vec3(sqrt(n / float(maxIteration))), 1);
	}
	else
	{
		color = vec4(0, 0, 0, 1);
	}
	imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), color);
}
