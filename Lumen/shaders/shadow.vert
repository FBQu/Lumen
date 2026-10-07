#version 450

layout(location = 0) in vec3 aPosition;

layout(push_constant) uniform Shadow
{
	mat4 uModel;
	mat4 uLightViewProj;
};

void main()
{
	gl_Position = uLightViewProj * uModel * vec4(aPosition, 1.0);
}
