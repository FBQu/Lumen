#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vTangent;

#include "frame.glsl"

// Environment (set 0): prefiltered specular cube, BRDF lookup table and their sampler.
layout(set = 0, binding = 0) uniform textureCube uEnvSpecular;
layout(set = 0, binding = 1) uniform texture2D uBrdfLut;
layout(set = 0, binding = 128) uniform sampler uEnvSampler;

// Directional light shadow map (depth, 0 = near) and a nearest-filter sampler.
layout(set = 0, binding = 3) uniform texture2D uShadowMap;
layout(set = 0, binding = 130) uniform sampler uShadowSampler;

layout(push_constant) uniform Draw
{
	mat4 uModel;
	vec4 uBaseColor;
	vec4 uMaterial;
	vec4 uEmissive;
	vec4 uParams;
};

// Material textures. nvrhi requires slots to be unique across the layouts of a pipeline, so they use slots 8..12 and
// sampler slot 8 (shader resource slot N -> binding N, sampler slot N -> binding 128 + N).
// Color textures are created with sRGB formats so sampling returns linear values.
layout(set = 1, binding = 8) uniform texture2D uBaseColorTex;
layout(set = 1, binding = 9) uniform texture2D uMetalRoughTex; // G roughness, B metallic
layout(set = 1, binding = 10) uniform texture2D uNormalTex;
layout(set = 1, binding = 11) uniform texture2D uOcclusionTex;  // R
layout(set = 1, binding = 12) uniform texture2D uEmissiveTex;
layout(set = 1, binding = 136) uniform sampler uSampler;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float DistributionGGX(float NoH, float a)
{
	float a2 = a * a;
	float d = NoH * NoH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

float GeometrySchlickGGX(float NoX, float roughness)
{
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	return NoX / (NoX * (1.0 - k) + k);
}

vec3 FresnelSchlick(float VoH, vec3 f0)
{
	return f0 + (1.0 - f0) * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
}

float InterleavedGradientNoise(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 VogelDisk(int index, int count, float phi)
{
	float r = sqrt((float(index) + 0.5) / float(count));
	float theta = float(index) * 2.39996323 + phi;
	return r * vec2(cos(theta), sin(theta));
}

// Percentage-closer soft shadows: a blocker search estimates the penumbra width (contact hardening), then a
// rotated Vogel-disk PCF filter of that width is applied. Returns 1 when fully lit.
float ShadowFactor(vec3 worldPos, vec3 geometricNormal)
{
	if (uShadowParams.x < 0.5)
		return 1.0;

	vec4 lightClip = uLightViewProj * vec4(worldPos + geometricNormal * uShadowParams.z, 1.0);
	vec3 proj = lightClip.xyz / lightClip.w;
	vec2 uv = vec2(proj.x * 0.5 + 0.5, 0.5 - proj.y * 0.5); // texture row 0 is the top (NDC y = +1)
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || proj.z > 1.0)
		return 1.0;

	float receiver = proj.z - uShadowParams.y;
	float rotation = InterleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
	float depthRange = uShadowParams2.x;
	float orthoWidth = uShadowParams2.y;
	float texel = uShadowParams2.z;
	float lightTan = uShadowParams.w;

	float searchRadius = clamp(lightTan * receiver * depthRange / orthoWidth, texel * 2.0, 0.05);
	float blockerSum = 0.0;
	float blockers = 0.0;
	for (int i = 0; i < 16; i++)
	{
		float d = textureLod(sampler2D(uShadowMap, uShadowSampler), uv + VogelDisk(i, 16, rotation) * searchRadius, 0.0).r;
		if (d < receiver)
		{
			blockerSum += d;
			blockers += 1.0;
		}
	}
	if (blockers < 0.5)
		return 1.0;

	float averageBlocker = blockerSum / blockers;
	float penumbra = clamp((receiver - averageBlocker) * depthRange * lightTan / orthoWidth, texel * 1.5, 0.06);
	float lit = 0.0;
	for (int i = 0; i < 24; i++)
	{
		float d = textureLod(sampler2D(uShadowMap, uShadowSampler), uv + VogelDisk(i, 24, rotation) * penumbra, 0.0).r;
		lit += receiver <= d ? 1.0 : 0.0;
	}
	return lit / 24.0;
}

vec3 FresnelSchlickRoughness(float NoV, vec3 f0, float roughness)
{
	return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
}

// Diffuse irradiance / pi from 9 SH coefficients (already convolved with the cosine lobe on the CPU).
vec3 EvalSH(vec3 n)
{
	return max(vec3(0.0),
		uSH[0].rgb * 0.282095
		+ uSH[1].rgb * (0.488603 * n.y) + uSH[2].rgb * (0.488603 * n.z) + uSH[3].rgb * (0.488603 * n.x)
		+ uSH[4].rgb * (1.092548 * n.x * n.y) + uSH[5].rgb * (1.092548 * n.y * n.z)
		+ uSH[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))
		+ uSH[7].rgb * (1.092548 * n.x * n.z) + uSH[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y)));
}

void main()
{
	vec4 baseSample = texture(sampler2D(uBaseColorTex, uSampler), vUV) * uBaseColor;
	float alphaMode = uParams.w;
	if (alphaMode > 0.5 && alphaMode < 1.5 && baseSample.a < uParams.z)
		discard;

	vec3 albedo = baseSample.rgb;
	vec3 mr = texture(sampler2D(uMetalRoughTex, uSampler), vUV).rgb;
	float metallic = clamp(uMaterial.x * mr.b, 0.0, 1.0);
	float roughness = clamp(uMaterial.y * mr.g, 0.045, 1.0);
	float a = roughness * roughness;

	// Shading normal: geometric normal, optionally perturbed by the tangent-space normal map.
	vec3 N = normalize(vNormal);
	if (!gl_FrontFacing)
		N = -N;
	vec3 T = vTangent.xyz - N * dot(N, vTangent.xyz);
	if (dot(T, T) > 1e-10)
	{
		T = normalize(T);
		vec3 B = cross(N, T) * vTangent.w;
		vec3 tn = texture(sampler2D(uNormalTex, uSampler), vUV).xyz * 2.0 - 1.0;
		tn.xy *= uParams.x;
		N = normalize(mat3(T, B, N) * tn);
	}

	vec3 V = normalize(uCameraPos.xyz - vWorldPos);
	vec3 L = normalize(-uLightDir.xyz);
	vec3 H = normalize(V + L);

	float NoV = max(dot(N, V), 1e-4);
	float NoL = max(dot(N, L), 0.0);
	float NoH = max(dot(N, H), 0.0);
	float VoH = max(dot(V, H), 0.0);

	vec3 f0 = mix(vec3(0.04), albedo, metallic);
	vec3 F = FresnelSchlick(VoH, f0);
	float D = DistributionGGX(NoH, a);
	float G = GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);

	vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
	vec3 diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI;

	vec3 radiance = uLightColor.rgb * uLightColor.w;
	vec3 geometricNormal = normalize(vNormal);
	if (!gl_FrontFacing)
		geometricNormal = -geometricNormal;
	float shadow = NoL > 0.0 ? ShadowFactor(vWorldPos, geometricNormal) : 1.0;
	vec3 color = (diffuse + specular) * radiance * NoL * shadow;

	float occlusion = 1.0 + uParams.y * (texture(sampler2D(uOcclusionTex, uSampler), vUV).r - 1.0);
	if (uEnvParams.z > 0.5)
	{
		// Image-based lighting: split-sum approximation with multiple-scattering energy compensation
		// (Fdez-Aguera 2019), so rough metals do not lose energy.
		vec3 Fenv = FresnelSchlickRoughness(NoV, f0, roughness);
		vec2 brdf = texture(sampler2D(uBrdfLut, uEnvSampler), vec2(NoV, roughness)).rg;
		vec3 FssEss = Fenv * brdf.x + brdf.y;
		float Ess = brdf.x + brdf.y;
		float Ems = 1.0 - Ess;
		vec3 Favg = f0 + (1.0 - f0) / 21.0;
		vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
		vec3 specularWeight = FssEss + Fms * Ems; // total specular reflectance including multiple scattering
		vec3 kD = (1.0 - metallic) * albedo * (1.0 - specularWeight);
		vec3 diffuseIbl = kD * EvalSH(N);
		vec3 R = reflect(-V, N);
		vec3 prefiltered = textureLod(samplerCube(uEnvSpecular, uEnvSampler), R, roughness * uEnvParams.y).rgb;
		vec3 specularIbl = prefiltered * specularWeight;
		color += (diffuseIbl + specularIbl) * uEnvParams.x * occlusion;
	}
	else
	{
		color += uAmbient.rgb * albedo * (1.0 - metallic) * occlusion;
	}
	color += uEmissive.rgb * texture(sampler2D(uEmissiveTex, uSampler), vUV).rgb;

	outColor = vec4(color, alphaMode > 1.5 ? baseSample.a : 1.0);
}
