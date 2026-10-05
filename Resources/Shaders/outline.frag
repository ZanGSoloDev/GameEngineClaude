#version 450
#extension GL_GOOGLE_include_directive : require

// Draws an outline around the selection mask (edge dilation), blended over the LDR image.

#include "Frame.glsl"

layout(set = 1, binding = 0) uniform texture2D uMask;

layout(push_constant) uniform PushBlock
{
	vec4 Color;  // rgb outline color, w thickness in pixels
} Push;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	float center = textureLod(sampler2D(uMask, uPointClamp), vUV, 0.0).r;
	float thickness = max(Push.Color.w, 1.0);
	float neighborMax = 0.0;
	for(int i = 0; i < 16; i++)
	{
		float a = float(i) * 0.39269908; // 2*pi/16
		vec2 offset = vec2(cos(a), sin(a)) * thickness * Frame.ScreenParams.zw;
		neighborMax = max(neighborMax, textureLod(sampler2D(uMask, uPointClamp), vUV + offset, 0.0).r);
	}
	float edge = neighborMax * (1.0 - center);
	// Selected pixels get a faint tint so overlapping silhouettes still read.
	float inner = center * 0.08;
	outColor = vec4(Push.Color.rgb, max(edge * 0.95, inner));
}
