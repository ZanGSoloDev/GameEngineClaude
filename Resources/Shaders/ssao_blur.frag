#version 450
#extension GL_GOOGLE_include_directive : require

// Separable depth-aware blur of the raw AO buffer.

#include "Frame.glsl"

layout(set = 1, binding = 0) uniform texture2D uAO;
layout(set = 1, binding = 1) uniform texture2D uDepth;

layout(push_constant) uniform PushBlock
{
	vec2 Direction; // (1,0) or (0,1)
	vec2 Padding;
} Push;

layout(location = 0) in vec2 vUV;
layout(location = 0) out float outAO;

void main()
{
	float centerDepth = LinearizeDepth(textureLod(sampler2D(uDepth, uPointClamp), vUV, 0.0).r);
	float sum = 0.0;
	float weightSum = 0.0;
	for(int i = -3; i <= 3; i++)
	{
		vec2 uv = vUV + Push.Direction * float(i) * Frame.ScreenParams.zw;
		float ao = textureLod(sampler2D(uAO, uPointClamp), uv, 0.0).r;
		float d = LinearizeDepth(textureLod(sampler2D(uDepth, uPointClamp), uv, 0.0).r);
		float spatial = exp(-float(i * i) / 8.0);
		float range = exp(-abs(d - centerDepth) / max(centerDepth * 0.02, 0.02));
		float w = spatial * range;
		sum += ao * w;
		weightSum += w;
	}
	outAO = sum / max(weightSum, 1e-4);
}
