#version 450
#extension GL_GOOGLE_include_directive : require

// Ground Truth Ambient Occlusion (Jimenez et al. 2016): slice based horizon search with analytic cosine-weighted integration.

#include "Frame.glsl"
#include "Pbr.glsl"

layout(set = 1, binding = 0) uniform texture2D uDepth;
layout(set = 1, binding = 1) uniform texture2D uNormal;

layout(location = 0) in vec2 vUV;
layout(location = 0) out float outAO;

vec3 ViewPos(vec2 uv)
{
	float d = textureLod(sampler2D(uDepth, uPointClamp), uv, 0.0).r;
	return ViewPositionFromDepth(uv, d);
}

void main()
{
	float depth = textureLod(sampler2D(uDepth, uPointClamp), vUV, 0.0).r;
	if(depth >= 1.0)
	{
		outAO = 1.0;
		return;
	}

	vec3 P = ViewPositionFromDepth(vUV, depth);
	vec3 N = normalize(textureLod(sampler2D(uNormal, uPointClamp), vUV, 0.0).xyz * 2.0 - 1.0);
	vec3 V = normalize(-P);

	float radius = Frame.SSAOParams.x;
	float pixelRadius = min(radius * Frame.Proj[1][1] * 0.5 * Frame.ScreenParams.y / max(-P.z, 1e-3), 192.0);
	if(pixelRadius < 1.5)
	{
		outAO = 1.0;
		return;
	}

	const int sliceCount = 3;
	int steps = clamp(int(Frame.SSAOParams.z) / 3, 4, 16);
	float noiseSlice = InterleavedGradientNoise(gl_FragCoord.xy);
	float noiseStep = fract(noiseSlice * 7.3 + 0.37);

	float visibility = 0.0;
	for(int s = 0; s < sliceCount; s++)
	{
		float phi = (float(s) + noiseSlice) / float(sliceCount) * PI;
		vec2 omega = vec2(cos(phi), sin(phi));         // uv-space direction (x right, y down)
		vec3 dirVec = vec3(omega.x, -omega.y, 0.0);    // matching view-space direction (y up)
		vec3 orthoDir = dirVec - dot(dirVec, V) * V;
		vec3 axis = normalize(cross(dirVec, V));
		vec3 projN = N - axis * dot(N, axis);
		float projNLen = length(projN);
		if(projNLen < 1e-4)
		{
			visibility += 1.0;
			continue;
		}
		float signN = sign(dot(orthoDir, projN));
		float cosN = clamp(dot(projN, V) / projNLen, 0.0, 1.0);
		float n = signN * acos(cosN);

		float cPos = -1.0; // cosine of the horizon angle along +dir
		float cNeg = -1.0; // ... along -dir
		for(int j = 0; j < steps; j++)
		{
			float t = (float(j) + noiseStep) / float(steps);
			t *= t;
			vec2 offsetUV = omega * (t * pixelRadius + 1.0) * Frame.ScreenParams.zw;
			vec3 dPos = ViewPos(vUV + offsetUV) - P;
			vec3 dNeg = ViewPos(vUV - offsetUV) - P;
			float lPos = length(dPos);
			float lNeg = length(dNeg);
			float hPos = dot(dPos, V) / max(lPos, 1e-5);
			float hNeg = dot(dNeg, V) / max(lNeg, 1e-5);
			// Samples beyond the radius fade out so distant geometry does not occlude.
			hPos = mix(hPos, -1.0, smoothstep(radius * 0.75, radius * 1.25, lPos));
			hNeg = mix(hNeg, -1.0, smoothstep(radius * 0.75, radius * 1.25, lNeg));
			cPos = max(cPos, hPos);
			cNeg = max(cNeg, hNeg);
		}

		float h1 = acos(clamp(cPos, -1.0, 1.0));
		float h0 = -acos(clamp(cNeg, -1.0, 1.0));
		h0 = n + max(h0 - n, -PI * 0.5);
		h1 = n + min(h1 - n, PI * 0.5);
		float arc0 = cosN + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n);
		float arc1 = cosN + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n);
		visibility += projNLen * (arc0 + arc1) * 0.25;
	}
	visibility = clamp(visibility / float(sliceCount), 0.0, 1.0);
	outAO = pow(visibility, Frame.SSAOParams.y);
}
