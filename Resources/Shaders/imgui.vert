#version 450

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform PushBlock
{
	vec2 Scale;
	vec2 Translate;
} Push;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
	vUV = aUV;
	vColor = aColor;
	gl_Position = vec4(aPosition * Push.Scale + Push.Translate, 0.0, 1.0);
}
