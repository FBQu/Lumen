#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vTangent;

layout(set = 0, binding = 256) uniform Frame
{
	mat4 uView;
	mat4 uProj;
	vec4 uCameraPos;
	vec4 uLightDir;
	vec4 uLightColor;
	vec4 uAmbient;
};

layout(push_constant) uniform Draw
{
	mat4 uModel;
	vec4 uBaseColor;
	vec4 uMaterial;
	vec4 uEmissive;
	vec4 uParams;
};

// Material textures (nvrhi: shader resource slot N -> binding N, sampler slot N -> binding 128 + N).
// Color textures are created with sRGB formats so sampling returns linear values.
layout(set = 1, binding = 0) uniform texture2D uBaseColorTex;
layout(set = 1, binding = 1) uniform texture2D uMetalRoughTex; // G roughness, B metallic
layout(set = 1, binding = 2) uniform texture2D uNormalTex;
layout(set = 1, binding = 3) uniform texture2D uOcclusionTex;  // R
layout(set = 1, binding = 4) uniform texture2D uEmissiveTex;
layout(set = 1, binding = 128) uniform sampler uSampler;

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
	vec3 color = (diffuse + specular) * radiance * NoL;

	float occlusion = 1.0 + uParams.y * (texture(sampler2D(uOcclusionTex, uSampler), vUV).r - 1.0);
	color += uAmbient.rgb * albedo * (1.0 - metallic) * occlusion;
	color += uEmissive.rgb * texture(sampler2D(uEmissiveTex, uSampler), vUV).rgb;

	outColor = vec4(color, alphaMode > 1.5 ? baseSample.a : 1.0);
}
