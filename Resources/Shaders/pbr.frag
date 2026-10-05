#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"
#include "Pbr.glsl"
#define MATERIAL_SET 2
#include "Material.glsl"
#include "SceneResources.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUV;
layout(location = 4) in float vViewDepth;

layout(location = 0) out vec4 outColor;

void main()
{
	vec2 uv = MaterialUV(vUV);
	vec4 baseSample = texture(sampler2D(uBaseColorTex, uMaterialSampler), uv);
	vec4 baseColor = baseSample * Material.BaseColor;

	float alphaMode = Material.Params1.y;
	if(alphaMode > 0.5 && alphaMode < 1.5 && baseColor.a < Material.Params1.x)
		discard;

	vec4 mr = texture(sampler2D(uMetalRoughTex, uMaterialSampler), uv);
	float roughness = clamp(Material.Params0.y * mr.g, 0.04, 1.0);
	float metallic = clamp(Material.Params0.x * mr.b, 0.0, 1.0);
	float occlusion = mix(1.0, texture(sampler2D(uOcclusionTex, uMaterialSampler), uv).r, Material.Params0.w);
	vec3 emissive = texture(sampler2D(uEmissiveTex, uMaterialSampler), uv).rgb * Material.EmissiveIntensity.rgb * Material.EmissiveIntensity.w;

	// Shading normal
	vec3 N = normalize(vNormal);
	vec3 V = normalize(Frame.CameraPosition.xyz - vWorldPos);
	bool backFacing = !gl_FrontFacing;
	if(backFacing && Material.Params1.z > 0.5)
		N = -N;
	vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
	vec3 B = cross(N, T) * vTangent.w;
	vec3 tangentNormal = texture(sampler2D(uNormalTex, uMaterialSampler), uv).xyz * 2.0 - 1.0;
	tangentNormal.xy *= Material.Params0.z;
	N = normalize(mat3(T, B, N) * tangentNormal);

	vec3 diffuseColor = baseColor.rgb * (1.0 - metallic);
	vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
	float NdotV = clamp(dot(N, V), 1e-4, 1.0);

	// Screen-space ambient occlusion
	vec2 screenUV = gl_FragCoord.xy * Frame.ScreenParams.zw;
	float ssao = Frame.SSAOParams.w > 0.5 ? texture(sampler2D(uSSAO, uLinearClamp), screenUV).r : 1.0;
	float ao = occlusion * ssao;

	vec3 color = vec3(0.0);

	// Directional light (sun)
	if(Frame.SunDirection.w > 0.5)
	{
		vec3 L = normalize(Frame.SunDirection.xyz);
		float shadow = SunShadow(vWorldPos, normalize(vNormal), L, vViewDepth, gl_FragCoord.xy);
		color += EvaluateDirectLight(N, V, L, Frame.SunColor.rgb * shadow, diffuseColor, F0, roughness);
	}

	// Point and spot lights
	for(int i = 0; i < Frame.LightCounts.x; i++)
	{
		LocalLight light = Lights[i];
		vec3 toLight = light.PositionRange.xyz - vWorldPos;
		float dist = length(toLight);
		if(dist >= light.PositionRange.w)
			continue;
		vec3 L = toLight / max(dist, 1e-4);
		float attenuation = RangeAttenuation(dist, light.PositionRange.w);
		if(light.DirectionType.w > 1.5)
			attenuation *= SpotAttenuation(L, light.DirectionType.xyz, light.SpotParams.x, light.SpotParams.y);
		if(attenuation <= 0.0)
			continue;
		float shadow = LocalLightShadow(light, vWorldPos, normalize(vNormal), L, dist, gl_FragCoord.xy);
		color += EvaluateDirectLight(N, V, L, light.ColorIntensity.rgb * attenuation * shadow, diffuseColor, F0, roughness);
	}

	// Image based lighting
	vec3 R = reflect(-V, N);
	vec3 Fenv = FresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 irradiance = textureLod(samplerCube(uIrradiance, uLinearClamp), RotateEnvironment(N), 0.0).rgb;
	vec3 kd = (1.0 - Fenv) * (1.0 - metallic);
	vec3 diffuseIBL = kd * baseColor.rgb * irradiance;
	vec3 prefiltered = textureLod(samplerCube(uPrefiltered, uLinearClamp), RotateEnvironment(R), roughness * Frame.EnvParams.y).rgb;
	vec2 brdf = texture(sampler2D(uBrdfLut, uLinearClamp), vec2(NdotV, roughness)).rg;
	vec3 specularIBL = prefiltered * (Fenv * brdf.x + brdf.y);
	// Horizon-based specular occlusion from AO
	float specularOcclusion = clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
	color += (diffuseIBL * ao + specularIBL * specularOcclusion) * Frame.EnvParams.x;

	color += emissive;

	float alpha = alphaMode > 1.5 ? baseColor.a : 1.0;
	outColor = vec4(color, alpha);
}
