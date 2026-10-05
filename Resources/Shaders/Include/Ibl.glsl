#ifndef IBL_GLSL
#define IBL_GLSL

const float PI = 3.14159265359;

// Direction through the center of texel (x, y) of a cube face, following the Vulkan/GL cube map face conventions
// (the inverse of the major-axis table in the specification), so textureCube(dir) returns what was written.
vec3 CubeDirection(uint face, vec2 uv)
{
	float sc = uv.x * 2.0 - 1.0;
	float tc = uv.y * 2.0 - 1.0;
	vec3 d;
	if(face == 0u)      d = vec3(1.0, -tc, -sc);
	else if(face == 1u) d = vec3(-1.0, -tc, sc);
	else if(face == 2u) d = vec3(sc, 1.0, tc);
	else if(face == 3u) d = vec3(sc, -1.0, -tc);
	else if(face == 4u) d = vec3(sc, -tc, 1.0);
	else                d = vec3(-sc, -tc, -1.0);
	return normalize(d);
}

float RadicalInverse(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint n)
{
	return vec2(float(i) / float(n), RadicalInverse(i));
}

// GGX importance sampling: returns the half vector in world space around N.
vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
	float a = roughness * roughness;
	float phi = 2.0 * PI * xi.x;
	float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
	vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);
	return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d + 1e-7);
}

#endif
