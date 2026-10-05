#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Starfall {

	constexpr uint32_t MaxCascades = 4;
	constexpr uint32_t MaxLocalLights = 256;
	constexpr uint32_t MaxLocalShadowLayers = 16;     // spot = 1 layer, point = 6 layers
	constexpr uint32_t LocalShadowMapSize = 1024;     // must match the constant in SceneResources.glsl

	// Camera used to render a view of the scene (editor camera or a CameraComponent).
	struct RenderCamera
	{
		glm::mat4 View = glm::mat4(1.0f);
		glm::mat4 Projection = glm::mat4(1.0f);
		glm::vec3 Position = { 0, 0, 0 };
		float Near = 0.1f;
		float Far = 500.0f;
		bool Orthographic = false;
	};

	// std140 layout, shared with Frame.glsl. Keep field order and sizes in sync.
	struct FrameData
	{
		glm::mat4 View;
		glm::mat4 Proj;
		glm::mat4 ViewProj;
		glm::mat4 InvView;
		glm::mat4 InvProj;
		glm::mat4 InvViewProj;
		glm::vec4 CameraPosition;     // xyz, w near
		glm::vec4 ScreenParams;       // width, height, 1/width, 1/height
		glm::vec4 TimeParams;         // time, delta, frame, far
		glm::vec4 EnvParams;          // ibl intensity, max prefilter lod, exposure, env rotation
		glm::vec4 SkyParams;          // skybox visible, skybox lod
		glm::vec4 SunDirection;       // xyz towards light, w has sun
		glm::vec4 SunColor;           // rgb radiance, w softness
		glm::vec4 SunShadowParams;    // bias, cascade count, map size, shadows enabled
		glm::mat4 CascadeViewProj[MaxCascades];
		glm::vec4 CascadeSplits;
		glm::vec4 CascadeWorldSize;
		glm::vec4 CascadeDepthRange;
		glm::mat4 LocalShadowViewProj[MaxLocalShadowLayers];
		glm::ivec4 LightCounts;
		glm::vec4 SSAOParams;         // radius, intensity, samples, enabled
	};

	// std430 layout, shared with SceneResources.glsl.
	struct LocalLightGPU
	{
		glm::vec4 PositionRange;
		glm::vec4 ColorIntensity;
		glm::vec4 DirectionType;
		glm::vec4 SpotParams;
		glm::vec4 ShadowParams;
	};
	static_assert(sizeof(LocalLightGPU) == 80);

	// std140, shared with Material.glsl.
	struct MaterialGPU
	{
		glm::vec4 BaseColor;
		glm::vec4 EmissiveIntensity;
		glm::vec4 Params0;
		glm::vec4 Params1;
		glm::vec4 UVTransform;
	};
	static_assert(sizeof(MaterialGPU) == 80);

	// 128 bytes of push constants used by all mesh draws.
	struct MeshPushConstants
	{
		glm::mat4 Model;
		glm::vec4 NormalColumn0;
		glm::vec4 NormalColumn1;
		glm::vec4 NormalColumn2;
		glm::vec4 Params;
	};
	static_assert(sizeof(MeshPushConstants) == 128);

}
