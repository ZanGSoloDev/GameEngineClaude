#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

layout(push_constant) uniform PushBlock
{
	mat4 Model;
	vec4 NormalCol0; // inverse-transpose of the model matrix (3x3), one column per vec4
	vec4 NormalCol1;
	vec4 NormalCol2;
	vec4 Params;     // x: 1 if the model matrix flips winding (negative determinant)
} Push;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;
layout(location = 4) out float vViewDepth;

void main()
{
	vec4 world = Push.Model * vec4(aPosition, 1.0);
	mat3 normalMatrix = mat3(Push.NormalCol0.xyz, Push.NormalCol1.xyz, Push.NormalCol2.xyz);

	vWorldPos = world.xyz;
	vNormal = normalize(normalMatrix * aNormal);
	vTangent = vec4(normalize(mat3(Push.Model) * aTangent.xyz), aTangent.w * (Push.Params.x > 0.5 ? -1.0 : 1.0));
	vUV = aUV;
	vec4 viewPos = Frame.View * world;
	vViewDepth = -viewPos.z;
	gl_Position = Frame.ViewProj * world;
}
