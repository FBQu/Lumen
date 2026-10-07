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
};
