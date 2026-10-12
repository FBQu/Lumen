#version 450

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform texture2D uSource;
layout(set = 0, binding = 128) uniform sampler uSampler;

layout(location = 0) out vec4 outColor;

// Copies (and scales, with bilinear filtering) an LDR image to the back buffer.
void main()
{
	outColor = vec4(texture(sampler2D(uSource, uSampler), vUV).rgb, 1.0);
}
