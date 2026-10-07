#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUV;

#include "frame.glsl"

layout(set = 0, binding = 0) uniform texture2D uDepth;
layout(set = 0, binding = 1) uniform texture2D uNormals; // view space
layout(set = 0, binding = 128) uniform sampler uPointSampler;

layout(location = 0) out float outAO;

const float PI = 3.14159265359;
const int DIRECTIONS = 6;
const int STEPS = 8;

float LinearDepth(float d)
{
	float n = uProjInfo.z;
	float f = uProjInfo.w;
	return n * f / (f - d * (f - n));
}

// View-space position (looking down -Z) of the pixel at uv with depth d.
vec3 ViewPos(vec2 uv, float d)
{
	float z = LinearDepth(d);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	return vec3(ndc * uProjInfo.xy * z, -z);
}

float InterleavedGradientNoise(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Horizon-based ambient occlusion: for several screen-space directions, march outward and track how high above the
// surface's tangent plane the occluding geometry rises (sin of the elevation angle), attenuated with distance.
void main()
{
	float depth = textureLod(sampler2D(uDepth, uPointSampler), vUV, 0.0).r;
	if (depth >= 1.0)
	{
		outAO = 1.0;
		return;
	}

	vec3 P = ViewPos(vUV, depth);
	vec3 N = normalize(textureLod(sampler2D(uNormals, uPointSampler), vUV, 0.0).xyz);

	float radius = uAOParams.x;
	float viewZ = -P.z;
	// World radius -> uv radius: the screen width at this depth is 2 * tanX * z world units.
	vec2 radiusUV = vec2(radius / (2.0 * uProjInfo.x * viewZ), radius / (2.0 * uProjInfo.y * viewZ));
	radiusUV = min(radiusUV, vec2(0.25)); // cap the kernel for very close surfaces

	float rotation = InterleavedGradientNoise(gl_FragCoord.xy);
	float jitter = InterleavedGradientNoise(gl_FragCoord.xy + vec2(47.0, 13.0));

	float occlusion = 0.0;
	for (int i = 0; i < DIRECTIONS; i++)
	{
		float angle = (float(i) + rotation) * (2.0 * PI / float(DIRECTIONS));
		vec2 dir = vec2(cos(angle), sin(angle));
		float maxSin = 0.0;
		for (int s = 0; s < STEPS; s++)
		{
			float t = (float(s) + jitter) / float(STEPS);
			t = t * t; // denser samples near the pixel
			vec2 uv = vUV + dir * radiusUV * t;
			// Snap to the texel center the point sampler will read, so the reconstructed position and the depth agree.
			uv = (floor(uv * uScreenParams.xy) + 0.5) * uScreenParams.zw;
			if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
				continue;
			float sampleDepth = textureLod(sampler2D(uDepth, uPointSampler), uv, 0.0).r;
			if (sampleDepth >= 1.0)
				continue;

			vec3 D = ViewPos(uv, sampleDepth) - P;
			float dist = length(D);
			if (dist < 1e-4)
				continue;
			float sinElevation = dot(N, D) / dist;
			float falloff = clamp(1.0 - (dist * dist) / (radius * radius), 0.0, 1.0);
			maxSin = max(maxSin, (sinElevation - 0.08) * falloff); // small bias avoids self-occlusion on curved surfaces
		}
		occlusion += maxSin;
	}
	occlusion /= float(DIRECTIONS);

	float ao = clamp(1.0 - occlusion * uAOParams.y * 1.6, 0.0, 1.0);
	outAO = pow(ao, uAOParams.z);
}
