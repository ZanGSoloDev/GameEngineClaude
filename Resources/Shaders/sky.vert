#version 450

// Fullscreen triangle at the far plane (depth = 1) so the sky only shows where nothing was drawn.
layout(location = 0) out vec2 vUV;

void main()
{
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	vUV = vec2(p.x, 1.0 - p.y);
	gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
