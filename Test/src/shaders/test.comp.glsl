#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0) uniform Frame
{
	uint time;
} frame;

layout(binding = 1, rgba8) uniform writeonly image2D resultImage;

layout(set = 1, binding = 0) uniform Transform
{
	mat4 matrix;
} transform;

uvec2 pictureSize = (gl_NumWorkGroups * gl_WorkGroupSize).xy;
uvec2 pixelCoords = gl_GlobalInvocationID.xy;
float floatTime = (frame.time & 0x0FFFFFFF) * .001f;
const float cellPeriod = 100.f;

float hash(vec2 v)
{
	return fract(sin(dot(v, vec2(267.983f, 808.233f))) * 48.5453f);
}

vec2 hash2(vec2 v)
{
	float v1 = hash(v);
	return vec2(v1, hash(v1 + v));
}

float grid(float period, float width)
{
  vec2 grid = fract(pixelCoords / period);
  return 1.f - step(width / period, grid.x) * step(width / period, grid.y);
}

float circle(vec2 position, float radius)
{
	return 1.f - smoothstep(radius * .5f, radius, length(position - pixelCoords));
}

void outputColor(vec3 color)
{
	imageStore(resultImage, ivec2(pixelCoords), vec4(color, 1));
}


float line(vec2 p1, vec2 p2, float width)
{
	vec2 pa = pixelCoords - p1;
	vec2 ba = p2 - p1;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.f, 1.f);	
    float d = length(pa - ba * h);
    float d2 = length(p1 - p2);
	return smoothstep(width * 2.f, width * .1f, d);
}

float smoothDist(vec2 p1, vec2 p2, float close, float far)
{
	return smoothstep(far, close, length(p1 - p2));
}

float fadingLine(vec2 p1, vec2 p2, float close, float far)
{
	float smoothLength = smoothDist(p1, p2, close, far);
	return line(p1, p2, smoothLength * 2.f) * smoothLength;
}

vec2 cellPoint(vec2 cellId)
{
	vec2 r = hash2(cellId + vec2(106.732f, 21.89f));
	return vec2(cos(floatTime * r.x), sin(floatTime * r.y)) * (cellPeriod * .4f) + (cellId + .5f) * cellPeriod;
}

void main()
{
	vec3 redGrid = grid(cellPeriod , 2.f) * vec3(1, 0, 0);

	vec2 cellId = floor(pixelCoords / cellPeriod);

	vec2 points[9];
	
	points[0] = cellPoint(cellId + vec2(-1, -1));
	points[1] = cellPoint(cellId + vec2(0, -1));
	points[2] = cellPoint(cellId + vec2(1, -1));
	
	points[3] = cellPoint(cellId + vec2(-1, 0));
	points[4] = cellPoint(cellId + vec2(0, 0));
	points[5] = cellPoint(cellId + vec2(1, 0));

	points[6] = cellPoint(cellId + vec2(-1, 1));
	points[7] = cellPoint(cellId + vec2(0, 1));
	points[8] = cellPoint(cellId + vec2(1, 1));

	const float close = cellPeriod * .8f;
	const float far = cellPeriod * 1.5f;

	float lineContainer = 0;

	lineContainer += fadingLine(points[0], points[4], close, far);
	lineContainer += fadingLine(points[1], points[4], close, far);
	lineContainer += fadingLine(points[2], points[4], close, far);
	lineContainer += fadingLine(points[3], points[4], close, far);
	
	lineContainer += fadingLine(points[5], points[4], close, far);
	lineContainer += fadingLine(points[6], points[4], close, far);
	lineContainer += fadingLine(points[7], points[4], close, far);
	lineContainer += fadingLine(points[8], points[4], close, far);

	lineContainer += fadingLine(points[1], points[3], close, far);
	lineContainer += fadingLine(points[1], points[5], close, far);
	lineContainer += fadingLine(points[3], points[7], close, far);
	lineContainer += fadingLine(points[5], points[7], close, far);

	vec3 lines = lineContainer * vec3(1);

	float pointModifier = 1.f;

	pointModifier += smoothDist(points[0], points[4], close, far);
	pointModifier += smoothDist(points[1], points[4], close, far);
	pointModifier += smoothDist(points[2], points[4], close, far);
	pointModifier += smoothDist(points[3], points[4], close, far);
	
	pointModifier += smoothDist(points[5], points[4], close, far);
	pointModifier += smoothDist(points[6], points[4], close, far);
	pointModifier += smoothDist(points[7], points[4], close, far);
	pointModifier += smoothDist(points[8], points[4], close, far);

	pointModifier *= .125f;

	vec3 point = vec3(circle(points[4], pointModifier * 15.f));

	vec3 lowColor = vec3(.1f, .1f, 0.f);
	vec3 highColor = vec3(.5f, .4f, .6f);
	vec3 clampedColor = point + lines;
	vec3 gradientColor = clamp(vec3(0.8f), vec3(0.2f), clampedColor) * mix(lowColor, highColor, clampedColor);

	vec3 resultColor = gradientColor;
	outputColor(resultColor);
}
