#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

// Requires MATERIAL_SET to be defined by the including shader (descriptor set index of the material layout).

layout(set = MATERIAL_SET, binding = 0, std140) uniform MaterialBlock
{
	vec4 BaseColor;
	vec4 EmissiveIntensity; // rgb emissive color, w intensity multiplier
	vec4 Params0;           // x metallic, y roughness, z normal scale, w occlusion strength
	vec4 Params1;           // x alpha cutoff, y alpha mode (0 opaque, 1 mask, 2 blend), z double sided
	vec4 UVTransform;       // xy scale, zw offset
} Material;

layout(set = MATERIAL_SET, binding = 1) uniform texture2D uBaseColorTex;
layout(set = MATERIAL_SET, binding = 2) uniform texture2D uMetalRoughTex;
layout(set = MATERIAL_SET, binding = 3) uniform texture2D uNormalTex;
layout(set = MATERIAL_SET, binding = 4) uniform texture2D uOcclusionTex;
layout(set = MATERIAL_SET, binding = 5) uniform texture2D uEmissiveTex;
layout(set = MATERIAL_SET, binding = 6) uniform sampler uMaterialSampler;

vec2 MaterialUV(vec2 uv) { return uv * Material.UVTransform.xy + Material.UVTransform.zw; }

#endif
