#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"
#include "Pbr.glsl"

layout(set = 1, binding = 0) uniform texture2D uHdr;

layout(push_constant) uniform PushBlock
{
	vec4 Params; // x tonemapper (0 none, 1 reinhard, 2 aces, 3 agx)
} Push;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const mat3 ACESInputMat = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
const mat3 ACESOutputMat = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));

vec3 RRTAndODTFit(vec3 v)
{
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return a / b;
}

vec3 ACESFilmic(vec3 color)
{
	color *= 1.0 / 0.6;
	color = ACESInputMat * color;
	color = RRTAndODTFit(color);
	color = ACESOutputMat * color;
	return clamp(color, 0.0, 1.0);
}

const mat3 AgXInset = mat3(
	vec3(0.842479062253094, 0.0423282422610123, 0.0423756549057051),
	vec3(0.0784335999999992, 0.878468636469772, 0.0784336),
	vec3(0.0792237451477643, 0.0791661274605434, 0.879142973793104));
const mat3 AgXOutset = mat3(
	vec3(1.19687900512017, -0.0528968517574562, -0.0529716355144438),
	vec3(-0.0980208811401368, 1.15190312990417, -0.0980434501171241),
	vec3(-0.0990297440797205, -0.0989611768448433, 1.15107367264116));

vec3 AgXContrast(vec3 x)
{
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 AgX(vec3 color)
{
	const float minEv = -12.47393;
	const float maxEv = 4.026069;
	color = AgXInset * color;
	color = max(color, vec3(1e-10));
	color = clamp(log2(color), minEv, maxEv);
	color = (color - minEv) / (maxEv - minEv);
	color = AgXContrast(color);
	color = AgXOutset * color;
	return clamp(pow(max(color, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
}

vec3 LinearToSRGB(vec3 c)
{
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
	return mix(lo, hi, step(vec3(0.0031308), c));
}

void main()
{
	vec3 hdr = texture(sampler2D(uHdr, uLinearClamp), vUV).rgb * Frame.EnvParams.z;
	hdr = max(hdr, vec3(0.0));
	if(!(hdr.r == hdr.r && hdr.g == hdr.g && hdr.b == hdr.b)) // NaN guard
		hdr = vec3(0.0);

	int mode = int(Push.Params.x + 0.5);
	vec3 ldr;
	if(mode == 1)
		ldr = hdr / (1.0 + hdr);
	else if(mode == 2)
		ldr = ACESFilmic(hdr);
	else if(mode == 3)
		ldr = AgX(hdr);
	else
		ldr = clamp(hdr, 0.0, 1.0);

	vec3 srgb = LinearToSRGB(clamp(ldr, 0.0, 1.0));
	srgb += (InterleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0; // dither to hide banding
	outColor = vec4(srgb, 1.0);
}
