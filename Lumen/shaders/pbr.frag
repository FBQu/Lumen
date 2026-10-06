#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

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
};

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
	vec3 N = normalize(vNormal);
	vec3 V = normalize(uCameraPos.xyz - vWorldPos);
	vec3 L = normalize(-uLightDir.xyz);
	vec3 H = normalize(V + L);

	vec3 albedo = uBaseColor.rgb;
	float metallic = clamp(uMaterial.x, 0.0, 1.0);
	float roughness = clamp(uMaterial.y, 0.045, 1.0);
	float a = roughness * roughness;

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
	color += uAmbient.rgb * albedo * (1.0 - metallic);
	color += uEmissive.rgb;

	outColor = vec4(color, uBaseColor.a);
}
