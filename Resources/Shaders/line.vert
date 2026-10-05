#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(location = 0) out vec4 vColor;

void main()
{
	vColor = aColor;
	gl_Position = Frame.ViewProj * vec4(aPosition, 1.0);
}
