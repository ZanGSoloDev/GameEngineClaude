#version 450
#extension GL_GOOGLE_include_directive : require

// Alpha-tested shadow caster: discards texels below the material alpha cutoff. Material is bound at set 0 here.
#define MATERIAL_SET 0
#include "Material.glsl"

layout(location = 0) in vec2 vUV;

void main()
{
	float alpha = texture(sampler2D(uBaseColorTex, uMaterialSampler), MaterialUV(vUV)).a * Material.BaseColor.a;
	if(alpha < Material.Params1.x)
		discard;
}
