#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Renderer/GraphicsDevice.h"
#include "Starfall/Renderer/Material.h"
#include "Starfall/Renderer/Mesh.h"
#include "Starfall/Renderer/RenderTypes.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <unordered_map>

namespace Starfall {

	// Shared GPU state used by all renderers: shaders, samplers, binding layouts, default textures, BRDF LUT.
	class RenderContext
	{
	public:
		// Returns null (after logging) if required resources (shaders) are missing.
		static Scope<RenderContext> Create(GraphicsDevice& graphicsDevice);
		~RenderContext();

		GraphicsDevice& GetGraphicsDevice() const { return m_GraphicsDevice; }
		nvrhi::IDevice* GetDevice() const { return m_GraphicsDevice.GetDevice(); }

		// Loads "<Resources>/Shaders/<name>.spv" (e.g. "pbr.frag"). Cached; null if missing.
		nvrhi::IShader* GetShader(const std::string& name);

		// Samplers
		nvrhi::ISampler* GetLinearClampSampler() const { return m_LinearClamp; }
		nvrhi::ISampler* GetPointClampSampler() const { return m_PointClamp; }
		nvrhi::ISampler* GetShadowCompareSampler() const { return m_ShadowCompare; }
		nvrhi::ISampler* GetMaterialSampler() const { return m_MaterialSampler; }
		nvrhi::ISampler* GetEquirectSampler() const { return m_EquirectSampler; }

		// Binding layouts shared by the passes.
		nvrhi::IBindingLayout* GetGlobalLayout() const { return m_GlobalLayout; }    // frame UBO + samplers + 128B push constants
		nvrhi::IBindingLayout* GetMaterialLayout() const { return m_MaterialLayout; }
		nvrhi::IBindingLayout* GetImGuiLayout() const { return m_ImGuiLayout; }

		// Creates the constant buffer / binding set of a material (and uploads its textures) if stale.
		// Requires an open command list.
		nvrhi::IBindingSet* PrepareMaterial(nvrhi::ICommandList* commandList, Material& material);

		// Uploads a mesh's buffers if needed.
		void PrepareMesh(nvrhi::ICommandList* commandList, Mesh& mesh);

		nvrhi::ITexture* GetBrdfLut() const { return m_BrdfLut; }
		// Computes the BRDF LUT on first call.
		void EnsureBrdfLut(nvrhi::ICommandList* commandList);

		nvrhi::BufferHandle CreateVolatileConstantBuffer(size_t byteSize, const char* name) const;

		// Compute helper: records a dispatch with the given pipeline/set/push constants.
		nvrhi::ComputePipelineHandle CreateComputePipeline(const std::string& shader, nvrhi::IBindingLayout* layout);

		static constexpr uint32_t BrdfLutSize = 256;

	private:
		explicit RenderContext(GraphicsDevice& graphicsDevice) : m_GraphicsDevice(graphicsDevice) {}
		bool Init();

		GraphicsDevice& m_GraphicsDevice;
		std::unordered_map<std::string, nvrhi::ShaderHandle> m_Shaders;
		nvrhi::SamplerHandle m_LinearClamp, m_PointClamp, m_ShadowCompare, m_MaterialSampler, m_EquirectSampler;
		nvrhi::BindingLayoutHandle m_GlobalLayout, m_MaterialLayout, m_ImGuiLayout;
		nvrhi::TextureHandle m_BrdfLut;
		bool m_BrdfLutBuilt = false;
	};

	nvrhi::VulkanBindingOffsets ZeroBindingOffsets();

}
