#ifndef SCENE_RESOURCES_GLSL
#define SCENE_RESOURCES_GLSL

// Scene-level resources of the forward pass (descriptor set 1): lights, shadow maps, image based lighting, SSAO.
// Requires Frame.glsl and Pbr.glsl.

struct LocalLight
{
	vec4 PositionRange;    // xyz position, w range
	vec4 ColorIntensity;   // rgb color * intensity
	vec4 DirectionType;    // xyz direction the light points to (spot), w type: 1 point, 2 spot
	vec4 SpotParams;       // x cos(inner), y cos(outer)
	vec4 ShadowParams;     // x first shadow layer (<0 none), y texel world size per unit distance, z depth bias, w softness
};

layout(set = 1, binding = 0, std430) readonly buffer LightBuffer { LocalLight Lights[]; };
layout(set = 1, binding = 1) uniform texture2DArray uSunShadow;
layout(set = 1, binding = 2) uniform texture2DArray uLocalShadow;
layout(set = 1, binding = 3) uniform textureCube uIrradiance;
layout(set = 1, binding = 4) uniform textureCube uPrefiltered;
layout(set = 1, binding = 5) uniform texture2D uBrdfLut;
layout(set = 1, binding = 6) uniform texture2D uSSAO;

vec3 RotateEnvironment(vec3 d)
{
	float c = cos(Frame.EnvParams.w);
	float s = sin(Frame.EnvParams.w);
	return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

vec2 VogelDisk(int index, int count, float phi)
{
	float r = sqrt((float(index) + 0.5) / float(count));
	float theta = float(index) * 2.39996323 + phi;
	return r * vec2(cos(theta), sin(theta));
}

float PCF(texture2DArray tex, vec2 uv, float layer, float refDepth, float radiusUV, float noise, int count)
{
	float sum = 0.0;
	for(int i = 0; i < count; i++)
	{
		vec2 offset = VogelDisk(i, count, noise * 6.2831853) * radiusUV;
		sum += texture(sampler2DArrayShadow(tex, uShadowCompare), vec4(uv + offset, layer, refDepth));
	}
	return sum / float(count);
}

// Returns shadow visibility for one cascade, or -1 if the position is outside it.
float SampleSunCascade(int cascade, vec3 worldPos, vec3 N, float NdotL, float noise)
{
	float mapSize = Frame.SunShadowParams.z;
	float texelWorld = Frame.CascadeWorldSize[cascade] / mapSize;
	vec3 offsetPos = worldPos + N * texelWorld * (1.5 + 2.0 * (1.0 - NdotL));
	vec4 lightClip = Frame.CascadeViewProj[cascade] * vec4(offsetPos, 1.0);
	vec3 ndc = lightClip.xyz / lightClip.w;
	vec2 uv = NdcToUV(ndc.xy);
	if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0 || ndc.z < 0.0)
		return -1.0;

	float layer = float(cascade);
	float refDepth = ndc.z - Frame.SunShadowParams.x;
	float texel = 1.0 / mapSize;
	float softness = Frame.SunColor.w;
	float depthRange = Frame.CascadeDepthRange[cascade];

	// PCSS: find the average blocker depth, derive the penumbra width, then filter.
	float searchRadius = clamp(softness * 4.0, 1.5, 12.0) * texel;
	float blockerSum = 0.0;
	float blockers = 0.0;
	for(int i = 0; i < 16; i++)
	{
		vec2 offset = VogelDisk(i, 16, noise * 6.2831853) * searchRadius;
		float d = textureLod(sampler2DArray(uSunShadow, uPointClamp), vec3(uv + offset, layer), 0.0).r;
		if(d < refDepth)
		{
			blockerSum += d;
			blockers += 1.0;
		}
	}
	if(blockers < 0.5)
		return 1.0;
	float avgBlocker = blockerSum / blockers;
	float penumbraWorld = (refDepth - avgBlocker) * depthRange * 0.02 * softness;
	float radiusTexels = clamp(penumbraWorld / texelWorld, 1.25, 14.0);
	return PCF(uSunShadow, uv, layer, refDepth, radiusTexels * texel, noise, 24);
}

float SunShadow(vec3 worldPos, vec3 N, vec3 L, float viewDepth, vec2 pixel)
{
	if(Frame.SunShadowParams.w < 0.5)
		return 1.0;
	int count = int(Frame.SunShadowParams.y);
	int cascade = 0;
	for(int i = 0; i < count - 1; i++)
		if(viewDepth > Frame.CascadeSplits[i])
			cascade = i + 1;

	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	float noise = InterleavedGradientNoise(pixel);
	float shadow = SampleSunCascade(cascade, worldPos, N, NdotL, noise);
	if(shadow < 0.0)
		return 1.0;

	// Blend into the next cascade near the boundary to hide the seam.
	if(cascade < count - 1)
	{
		float splitEnd = Frame.CascadeSplits[cascade];
		float blendStart = splitEnd * 0.9;
		if(viewDepth > blendStart)
		{
			float next = SampleSunCascade(cascade + 1, worldPos, N, NdotL, noise);
			if(next >= 0.0)
				shadow = mix(shadow, next, smoothstep(blendStart, splitEnd, viewDepth));
		}
	}
	// Fade out at the maximum shadow distance.
	float fade = 1.0 - smoothstep(Frame.CascadeSplits[count - 1] * 0.9, Frame.CascadeSplits[count - 1], viewDepth);
	return mix(1.0, shadow, fade);
}

float LocalLightShadow(LocalLight light, vec3 worldPos, vec3 N, vec3 L, float dist, vec2 pixel)
{
	float baseLayer = light.ShadowParams.x;
	if(baseLayer < 0.0)
		return 1.0;
	int layer = int(baseLayer);
	if(light.DirectionType.w < 1.5)
	{
		vec3 d = worldPos - light.PositionRange.xyz;
		vec3 a = abs(d);
		int face;
		if(a.x >= a.y && a.x >= a.z)
			face = d.x > 0.0 ? 0 : 1;
		else if(a.y >= a.z)
			face = d.y > 0.0 ? 2 : 3;
		else
			face = d.z > 0.0 ? 4 : 5;
		layer += face;
	}

	float texelWorld = light.ShadowParams.y * dist;
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	vec3 offsetPos = worldPos + N * texelWorld * (1.5 + 2.0 * (1.0 - NdotL));
	vec4 lightClip = Frame.LocalShadowViewProj[layer] * vec4(offsetPos, 1.0);
	vec3 ndc = lightClip.xyz / lightClip.w;
	vec2 uv = NdcToUV(ndc.xy);
	if(lightClip.w <= 0.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
		return 1.0;

	float noise = InterleavedGradientNoise(pixel);
	float radiusTexels = 1.25 + light.ShadowParams.w * 2.0;
	return PCF(uLocalShadow, uv, float(layer), ndc.z - light.ShadowParams.z, radiusTexels / 1024.0, noise, 16);
}

#endif
