#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

layout(push_constant) uniform PushBlock
{
	mat4 LightViewProjModel;
} Push;

layout(location = 0) out vec2 vUV;

void main()
{
	vUV = aUV;
	gl_Position = Push.LightViewProjModel * vec4(aPosition, 1.0);
}
