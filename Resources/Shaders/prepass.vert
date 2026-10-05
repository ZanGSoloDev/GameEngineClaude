#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

layout(push_constant) uniform PushBlock
{
	mat4 Model;
	vec4 NormalCol0;
	vec4 NormalCol1;
	vec4 NormalCol2;
	vec4 Params;
} Push;

layout(location = 0) out vec3 vViewNormal;
layout(location = 1) out vec2 vUV;

void main()
{
	vec4 world = Push.Model * vec4(aPosition, 1.0);
	mat3 normalMatrix = mat3(Push.NormalCol0.xyz, Push.NormalCol1.xyz, Push.NormalCol2.xyz);
	vViewNormal = mat3(Frame.View) * normalize(normalMatrix * aNormal);
	vUV = aUV;
	gl_Position = Frame.ViewProj * world;
}
