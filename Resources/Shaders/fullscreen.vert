#version 450

// Fullscreen triangle. uv (0,0) is the top-left corner of the render target.
layout(location = 0) out vec2 vUV;

void main()
{
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	vUV = vec2(p.x, 1.0 - p.y);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
