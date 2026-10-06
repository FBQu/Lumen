#version 450

layout(location = 0) out vec2 vUV;

// Single oversized triangle covering the screen; uv (0,0) is the top-left corner.
void main()
{
	vUV = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
