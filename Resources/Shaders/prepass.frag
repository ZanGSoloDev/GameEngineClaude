#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"
#define MATERIAL_SET 1
#include "Material.glsl"

layout(location = 0) in vec3 vViewNormal;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 outNormal;

void main()
{
	if(Material.Params1.y > 0.5 && Material.Params1.y < 1.5)
	{
		float alpha = texture(sampler2D(uBaseColorTex, uMaterialSampler), MaterialUV(vUV)).a * Material.BaseColor.a;
		if(alpha < Material.Params1.x)
			discard;
	}
	vec3 n = normalize(vViewNormal);
	if(!gl_FrontFacing)
		n = -n;
	outNormal = vec4(n * 0.5 + 0.5, 1.0);
}
