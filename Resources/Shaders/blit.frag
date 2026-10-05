#version 450

layout(set = 0, binding = 0) uniform texture2D uSource;
layout(set = 0, binding = 1) uniform sampler uSampler;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(texture(sampler2D(uSource, uSampler), vUV).rgb, 1.0);
}
