#version 450

layout(location = 0) in vec2 vUV;

// nvrhi Vulkan binding offsets: shader resource slot N -> binding N, sampler slot N -> binding 128 + N.
layout(set = 0, binding = 0) uniform texture2D uHdr;
layout(set = 0, binding = 128) uniform sampler uSampler;

layout(push_constant) uniform Params
{
	float uExposure;
};

layout(location = 0) out vec4 outColor;

// Narkowicz's ACES filmic approximation.
vec3 AcesFilm(vec3 x)
{
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 LinearToSrgb(vec3 c)
{
	vec3 low = c * 12.92;
	vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
	return mix(high, low, lessThanEqual(c, vec3(0.0031308)));
}

void main()
{
	vec3 hdr = texture(sampler2D(uHdr, uSampler), vUV).rgb * uExposure;
	outColor = vec4(LinearToSrgb(AcesFilm(hdr)), 1.0);
}
