#ifndef PBR_GLSL
#define PBR_GLSL

// Cook-Torrance GGX microfacet BRDF helpers (metallic-roughness workflow).

float DistributionGGX(float NdotH, float alpha)
{
	float a2 = alpha * alpha;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d + 1e-7);
}

// Height-correlated Smith visibility term (already divided by 4 NdotL NdotV).
float VisibilitySmithGGX(float NdotV, float NdotL, float alpha)
{
	float a2 = alpha * alpha;
	float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
	float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
	return 0.5 / (gv + gl + 1e-7);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Direct light contribution for one light direction L (towards the light) with the given incoming radiance.
vec3 EvaluateDirectLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 diffuseColor, vec3 F0, float roughness)
{
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	if(NdotL <= 0.0)
		return vec3(0.0);
	vec3 H = normalize(V + L);
	float NdotV = max(dot(N, V), 1e-4);
	float NdotH = clamp(dot(N, H), 0.0, 1.0);
	float VdotH = clamp(dot(V, H), 0.0, 1.0);
	float alpha = max(roughness * roughness, 0.002);

	float D = DistributionGGX(NdotH, alpha);
	float Vis = VisibilitySmithGGX(NdotV, NdotL, alpha);
	vec3 F = FresnelSchlick(VdotH, F0);

	vec3 specular = D * Vis * F;
	vec3 kd = (1.0 - F);
	vec3 diffuse = kd * diffuseColor / PI;
	return (diffuse + specular) * radiance * NdotL;
}

// Smooth distance window so lights reach exactly zero at `range`.
float RangeAttenuation(float distance, float range)
{
	float d2 = distance * distance;
	float window = clamp(1.0 - pow(distance / max(range, 1e-3), 4.0), 0.0, 1.0);
	return window * window / max(d2, 0.01);
}

float SpotAttenuation(vec3 L, vec3 spotForward, float cosInner, float cosOuter)
{
	float cd = dot(-L, spotForward);
	float t = clamp((cd - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
	return t * t;
}

// Interleaved gradient noise (Jimenez), stable per pixel.
float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#endif
