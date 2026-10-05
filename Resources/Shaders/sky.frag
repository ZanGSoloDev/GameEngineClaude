#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"

layout(set = 1, binding = 0) uniform textureCube uEnvironment;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 RotateEnv(vec3 d)
{
	float c = cos(Frame.EnvParams.w);
	float s = sin(Frame.EnvParams.w);
	return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

void main()
{
	vec4 world = Frame.InvViewProj * vec4(UVToNdc(vUV), 1.0, 1.0);
	vec3 dir = normalize(world.xyz / world.w - Frame.CameraPosition.xyz);
	vec3 color = textureLod(samplerCube(uEnvironment, uLinearClamp), RotateEnv(dir), Frame.SkyParams.y).rgb * Frame.EnvParams.x;
	outColor = vec4(color, 1.0);
}
