#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Renderer/RenderContext.h"
#include "Starfall/Scene/SceneSettings.h"

#include <string>

namespace Starfall {

	// Image based lighting resources for one environment: the source cube (skybox), a diffuse irradiance cube and a GGX prefiltered
	// specular cube. Built on the GPU from an equirectangular HDRI, or from a procedural sky gradient when no HDRI is set.
	class EnvironmentMap
	{
	public:
		static constexpr uint32_t EnvironmentSize = 512;
		static constexpr uint32_t IrradianceSize = 32;
		static constexpr uint32_t PrefilterSize = 256;

		explicit EnvironmentMap(RenderContext& context);

		// Rebuilds the maps when the settings that affect them changed. Needs an open command list. Returns true if rebuilt.
		bool Update(nvrhi::ICommandList* commandList, const EnvironmentSettings& settings);

		nvrhi::ITexture* GetEnvironment() const { return m_Environment; }
		nvrhi::ITexture* GetIrradiance() const { return m_Irradiance; }
		nvrhi::ITexture* GetPrefiltered() const { return m_Prefiltered; }
		float GetMaxPrefilterLod() const { return static_cast<float>(m_PrefilterMips - 1); }
		const std::string& GetSourceDescription() const { return m_Key; }

	private:
		void BuildFromEquirect(nvrhi::ICommandList* commandList, const HdrImageData& image);
		void BuildProcedural(nvrhi::ICommandList* commandList, const EnvironmentSettings& settings);
		void FilterEnvironment(nvrhi::ICommandList* commandList);

		RenderContext& m_Context;
		nvrhi::TextureHandle m_Environment, m_Irradiance, m_Prefiltered;
		uint32_t m_EnvironmentMips = 1;
		uint32_t m_PrefilterMips = 1;
		std::string m_Key;

		nvrhi::BindingLayoutHandle m_EquirectLayout, m_GradientLayout, m_FilterLayout;
		nvrhi::ComputePipelineHandle m_EquirectPipeline, m_GradientPipeline, m_IrradiancePipeline, m_PrefilterPipeline;
	};

	// Float32 -> IEEE half conversion (round to nearest even, saturating to the largest finite half).
	uint16_t FloatToHalf(float value);

}
