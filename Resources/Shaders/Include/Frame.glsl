#ifndef FRAME_GLSL
#define FRAME_GLSL

// Per-frame data shared by all scene passes. Must match FrameData in Renderer/RenderTypes.h (std140).
// NOTE: render targets are rendered with a flipped viewport (nvrhi), so framebuffer uv (0,0) is the top-left corner
// and uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5).

layout(set = 0, binding = 0, std140) uniform FrameBlock
{
	mat4 View;
	mat4 Proj;
	mat4 ViewProj;
	mat4 InvView;
	mat4 InvProj;
	mat4 InvViewProj;
	vec4 CameraPosition;     // xyz position, w near plane
	vec4 ScreenParams;       // width, height, 1/width, 1/height
	vec4 TimeParams;         // time, delta time, frame index, far plane
	vec4 EnvParams;          // x ibl intensity, y max prefilter lod, z exposure, w env rotation (radians)
	vec4 SkyParams;          // x skybox visible, y skybox lod
	vec4 SunDirection;       // xyz direction TOWARDS the light, w has sun
	vec4 SunColor;           // rgb radiance, w softness
	vec4 SunShadowParams;    // x bias, y cascade count, z shadow map size, w shadows enabled
	mat4 CascadeViewProj[4];
	vec4 CascadeSplits;      // view-space distance at the end of each cascade
	vec4 CascadeWorldSize;   // world width of each cascade
	vec4 CascadeDepthRange;  // world depth range of each cascade
	mat4 LocalShadowViewProj[16];
	ivec4 LightCounts;       // x local light count
	vec4 SSAOParams;         // x radius, y intensity, z sample count, w enabled
} Frame;

layout(set = 0, binding = 1) uniform sampler uLinearClamp;
layout(set = 0, binding = 2) uniform samplerShadow uShadowCompare;
layout(set = 0, binding = 3) uniform sampler uPointClamp;

const float PI = 3.14159265359;

vec2 NdcToUV(vec2 ndc) { return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5); }
vec2 UVToNdc(vec2 uv) { return vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0); }

// Depth buffer value (0..1) to view-space position.
vec3 ViewPositionFromDepth(vec2 uv, float depth)
{
	vec4 clip = vec4(UVToNdc(uv), depth, 1.0);
	vec4 view = Frame.InvProj * clip;
	return view.xyz / view.w;
}

float LinearizeDepth(float depth)
{
	float n = Frame.CameraPosition.w;
	float f = Frame.TimeParams.w;
	return n * f / (f - depth * (f - n));
}

#endif
