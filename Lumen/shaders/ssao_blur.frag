#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUV;

#include "frame.glsl"

layout(set = 0, binding = 0) uniform texture2D uAO;
layout(set = 0, binding = 1) uniform texture2D uDepth;
layout(set = 0, binding = 128) uniform sampler uPointSampler;

layout(push_constant) uniform Blur
{
	vec2 uDirection; // (1/width, 0) or (0, 1/height)
};

layout(location = 0) out float outAO;

float LinearDepth(float d)
{
	float n = uProjInfo.z;
	float f = uProjInfo.w;
	return n * f / (f - d * (f - n));
}

// Separable depth-aware blur: neighbors at a very different depth do not contribute, so occlusion does not bleed
// across silhouettes.
void main()
{
	float centerDepth = textureLod(sampler2D(uDepth, uPointSampler), vUV, 0.0).r;
	float centerAO = textureLod(sampler2D(uAO, uPointSampler), vUV, 0.0).r;
	if (centerDepth >= 1.0)
	{
		outAO = 1.0;
		return;
	}

	float centerZ = LinearDepth(centerDepth);
	float sum = centerAO;
	float weightSum = 1.0;
	for (int i = -4; i <= 4; i++)
	{
		if (i == 0)
			continue;
		vec2 uv = vUV + uDirection * float(i);
		float d = textureLod(sampler2D(uDepth, uPointSampler), uv, 0.0).r;
		if (d >= 1.0)
			continue;
		float z = LinearDepth(d);
		float spatial = exp(-float(i * i) / 18.0);
		float range = max(0.0, 1.0 - abs(z - centerZ) / (0.05 * centerZ + 0.02));
		float w = spatial * range;
		sum += textureLod(sampler2D(uAO, uPointSampler), uv, 0.0).r * w;
		weightSum += w;
	}
	outAO = sum / weightSum;
}
