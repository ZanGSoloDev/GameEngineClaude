#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"

layout(push_constant) uniform PushBlock
{
	vec4 Params; // x half extent of the quad, y/z center xz
} Push;

layout(location = 0) out vec3 vWorldPos;

void main()
{
	// Two triangles forming a quad on the y = 0 plane around the camera.
	vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
	vec2 c = corners[gl_VertexIndex];
	vec3 world = vec3(Push.Params.y + c.x * Push.Params.x, 0.0, Push.Params.z + c.y * Push.Params.x);
	vWorldPos = world;
	gl_Position = Frame.ViewProj * vec4(world, 1.0);
}
