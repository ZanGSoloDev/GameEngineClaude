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
	vec4 Unused0;
	vec4 Unused1;
	vec4 Unused2;
	vec4 Params;
} Push;

void main()
{
	gl_Position = Frame.ViewProj * (Push.Model * vec4(aPosition, 1.0));
}
