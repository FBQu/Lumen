#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;

#include "frame.glsl"

layout(push_constant) uniform Draw
{
	mat4 uModel;
	vec4 uBaseColor;
	vec4 uMaterial;  // x metallic, y roughness
	vec4 uEmissive;
	vec4 uParams;    // x normalScale, y occlusionStrength, z alphaCutoff, w alphaMode (0 opaque, 1 mask, 2 blend)
};

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vTangent;

void main()
{
	vec4 world = uModel * vec4(aPosition, 1.0);
	vWorldPos = world.xyz;
	mat3 normalMatrix = mat3(transpose(inverse(uModel)));
	vNormal = normalMatrix * aNormal;
	vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
	vUV = aUV;
	gl_Position = uProj * uView * world;
}
