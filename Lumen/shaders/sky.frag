#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUV;

#include "frame.glsl"

layout(set = 0, binding = 2) uniform texture2D uBackground;  // equirectangular HDR
layout(set = 0, binding = 129) uniform sampler uSkySampler;   // wraps horizontally, clamps vertically

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

void main()
{
	// Reconstruct the world-space view ray through this pixel (uv (0,0) is the top-left corner, NDC y is up).
	vec4 ndc = vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 1.0, 1.0);
	vec4 world = uInvViewProj * ndc;
	vec3 dir = normalize(world.xyz / world.w - uCameraPos.xyz);

	// Same mapping as EnvironmentBuilder::DirectionToEquirect.
	vec2 uv = vec2(atan(dir.x, -dir.z) / (2.0 * PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / PI);
	outColor = vec4(textureLod(sampler2D(uBackground, uSkySampler), uv, 0.0).rgb * uEnvParams.x, 1.0);
}
