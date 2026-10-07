// Per-frame data shared by all scene shaders. Must match FrameConstants in Renderer.cpp (std140).
// nvrhi Vulkan binding offsets: constant buffer slot N -> binding 256 + N.
layout(set = 0, binding = 256) uniform Frame
{
	mat4 uView;
	mat4 uProj;
	mat4 uInvViewProj;
	vec4 uCameraPos;
	vec4 uLightDir;   // direction the light travels (world space)
	vec4 uLightColor; // rgb color, w intensity
	vec4 uAmbient;    // rgb constant ambient radiance, used when there is no environment
	vec4 uSH[9];      // diffuse irradiance / pi as spherical harmonics (premultiplied by the cosine lobe)
	vec4 uEnvParams;  // x intensity, y highest specular mip, z 1 if an environment is set, w 1 to draw it as background
	mat4 uLightViewProj;
	vec4 uShadowParams;  // x 1 if shadows are on, y receiver depth bias, z normal offset (world units), w tan(light angular radius)
	vec4 uShadowParams2; // x depth range (world units), y ortho width (world units), z texel size in uv
	vec4 uProjInfo;      // x tan(fovY/2) * aspect, y tan(fovY/2), z near, w far
	vec4 uAOParams;      // x radius (world units), y intensity, z power, w 1 if ambient occlusion is on
	vec4 uScreenParams;  // x width, y height, z 1/width, w 1/height
};
