#version 450
#extension GL_GOOGLE_include_directive : require

#include "Frame.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

float GridLine(vec2 coord)
{
	vec2 derivative = fwidth(coord);
	vec2 grid = abs(fract(coord - 0.5) - 0.5) / max(derivative, vec2(1e-5));
	return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main()
{
	vec2 xz = vWorldPos.xz;
	float minor = GridLine(xz);
	float major = GridLine(xz / 10.0);

	// World axes
	vec2 axisDerivative = fwidth(xz);
	float axisX = 1.0 - min(abs(xz.y) / max(axisDerivative.y, 1e-5), 1.0); // line along X (z == 0)
	float axisZ = 1.0 - min(abs(xz.x) / max(axisDerivative.x, 1e-5), 1.0); // line along Z (x == 0)

	vec3 color = vec3(0.55);
	float alpha = max(minor * 0.18, major * 0.35);
	if(axisX > 0.0) { color = vec3(0.9, 0.25, 0.25); alpha = max(alpha, axisX * 0.9); }
	if(axisZ > 0.0) { color = vec3(0.3, 0.45, 0.95); alpha = max(alpha, axisZ * 0.9); }

	float dist = length(vWorldPos - Frame.CameraPosition.xyz);
	float fade = 1.0 - smoothstep(60.0, 250.0, dist);
	// Fade grazing angles to avoid moire.
	float viewAngle = abs(normalize(Frame.CameraPosition.xyz - vWorldPos).y);
	fade *= smoothstep(0.0, 0.15, viewAngle);

	outColor = vec4(color, alpha * fade);
}
