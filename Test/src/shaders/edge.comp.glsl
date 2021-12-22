#version 450
#extension GL_ARB_separate_shader_objects : enable
#pragma optionNV(unroll all)

layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba8) uniform writeonly image2D resultImage;
layout(set = 0, binding = 1) uniform sampler2D sourceImage;

layout(set = 1, binding = 0) uniform Transform
{
	mat3 matrix;
} transform;

mat3 sx = mat3( 
    1.0, 2.0, 1.0, 
    0.0, 0.0, 0.0, 
   -1.0, -2.0, -1.0 
);
mat3 sy = mat3( 
    1.0, 0.0, -1.0, 
    2.0, 0.0, -2.0, 
    1.0, 0.0, -1.0 
);

void main()
{
    const vec3 edgeColor = vec3(0, 0, 0);

	vec3 original = texelFetch(sourceImage, ivec2(gl_GlobalInvocationID.xy), 0).rgb;
    
    mat3 R, G, B;
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            vec3 s  = texelFetch(sourceImage, ivec2(gl_GlobalInvocationID.xy) + ivec2(i - 1,j - 1), 0 ).rgb;
            R[i][j] = s.r;
            G[i][j] = s.g;
            B[i][j] = s.b;
		}
	}

	float rgx = dot(sx[0], R[0]) + dot(sx[1], R[1]) + dot(sx[2], R[2]); 
	float rgy = dot(sy[0], R[0]) + dot(sy[1], R[1]) + dot(sy[2], R[2]);

	float rg = sqrt(pow(rgx, 2) + pow(rgy, 2));

    float ggx = dot(sx[0], G[0]) + dot(sx[1], G[1]) + dot(sx[2], G[2]); 
	float ggy = dot(sy[0], G[0]) + dot(sy[1], G[1]) + dot(sy[2], G[2]);

	float gg = sqrt(pow(ggx, 2) + pow(ggy, 2));

    float bgx = dot(sx[0], B[0]) + dot(sx[1], B[1]) + dot(sx[2], B[2]); 
	float bgy = dot(sy[0], B[0]) + dot(sy[1], B[1]) + dot(sy[2], B[2]);

	float bg = sqrt(pow(bgx, 2) + pow(bgy, 2));

    vec4 color = vec4(mix(original, edgeColor, max(max(rg, gg), bg)), 1.);
	imageStore(resultImage, ivec2(gl_GlobalInvocationID.xy), color);
}
