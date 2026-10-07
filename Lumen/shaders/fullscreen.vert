#version 450

layout(location = 0) out vec2 vUV;

// Single oversized triangle covering the screen. vUV (0,0) is the top-left corner of the image, matching texture
// row 0. nvrhi uses +Y-up clip space, so the top of the screen is NDC y = +1.
void main()
{
	vUV = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 0.0, 1.0);
}
