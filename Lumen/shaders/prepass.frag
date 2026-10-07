#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vTangent;

#include "frame.glsl"

layout(push_constant) uniform Draw
{
	mat4 uModel;
	vec4 uBaseColor;
	vec4 uMaterial;
	vec4 uEmissive;
	vec4 uParams;
};

// View-space geometric normal for the ambient occlusion pass (depth is written by the fixed-function pipeline).
layout(location = 0) out vec4 outNormal;

void main()
{
	vec3 N = normalize(vNormal);
	if (!gl_FrontFacing)
		N = -N;
	outNormal = vec4(normalize(mat3(uView) * N), 1.0);
}
