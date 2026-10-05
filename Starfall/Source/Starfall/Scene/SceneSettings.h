#pragma once

#include "Starfall/Scene/Components.h"

namespace Starfall {

	enum class Tonemapper { None = 0, Reinhard = 1, ACES = 2, AgX = 3 };

	struct EnvironmentSettings
	{
		AssetPath HDRI;                         // empty = procedural gradient sky
		float Intensity = 1.0f;
		float RotationDegrees = 0.0f;
		bool ShowSkybox = true;
		glm::vec3 SkyColor = { 0.35f, 0.55f, 0.85f }; // procedural sky (no HDRI)
		glm::vec3 GroundColor = { 0.25f, 0.22f, 0.20f };
	};

	struct PostProcessSettings
	{
		float Exposure = 1.0f;
		Tonemapper TonemapMode = Tonemapper::ACES;
		bool SSAOEnabled = true;
		float SSAORadius = 0.6f;
		float SSAOIntensity = 1.2f;
		int SSAOSamples = 16;
	};

	struct ShadowSettings
	{
		uint32_t MapSize = 2048;
		float MaxDistance = 80.0f;
		uint32_t CascadeCount = 4;
		float CascadeSplitLambda = 0.8f;
	};

	struct SceneSettings
	{
		EnvironmentSettings Environment;
		PostProcessSettings PostProcess;
		ShadowSettings Shadows;
		glm::vec3 Gravity = { 0.0f, -9.81f, 0.0f };
	};

}
