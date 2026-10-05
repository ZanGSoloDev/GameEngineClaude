#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Core/UUID.h"
#include "Starfall/Renderer/DebugRenderer.h"
#include "Starfall/Renderer/EnvironmentMap.h"
#include "Starfall/Renderer/RenderContext.h"
#include "Starfall/Renderer/RenderTypes.h"
#include "Starfall/Scene/Scene.h"

#include <vector>

namespace Starfall {

	struct SceneRenderOptions
	{
		float Time = 0.0f;
		float DeltaTime = 0.0f;
		uint64_t Frame = 0;
		bool DrawGrid = false;               // editor ground grid
		bool EnableShadows = true;
		bool EnableSSAO = true;
		bool Wireframe = false;              // not supported yet; reserved
		std::vector<UUID> OutlinedEntities;  // selection outline (entities with a MeshRenderer)
		glm::vec4 OutlineColor = { 1.0f, 0.6f, 0.1f, 1.0f };
	};

	struct RenderStats
	{
		uint32_t DrawCalls = 0;
		uint32_t ShadowDrawCalls = 0;
		uint32_t Triangles = 0;
		uint32_t MeshesSubmitted = 0;
		uint32_t MeshesCulled = 0;
		uint32_t LocalLights = 0;
		uint32_t ShadowedLocalLights = 0;
		uint32_t Cascades = 0;
	};

	// Renders a Scene to an LDR RGBA8 texture:
	//   shadow maps (CSM + spot/point) -> depth/normal prepass -> GTAO -> forward PBR + IBL -> sky -> transparency
	//   -> tonemapping -> editor overlays (grid, debug lines, selection outline).
	class SceneRenderer
	{
	public:
		explicit SceneRenderer(RenderContext& context);
		~SceneRenderer();
		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		// Records all passes into an open command list. The result is available through GetOutput().
		void Render(nvrhi::ICommandList* commandList, Scene& scene, const RenderCamera& camera, uint32_t width, uint32_t height, const SceneRenderOptions& options);

		nvrhi::ITexture* GetOutput() const { return m_Ldr; }
		nvrhi::ITexture* GetHdrOutput() const { return m_Hdr; }
		nvrhi::ITexture* GetDepth() const { return m_Depth; }
		nvrhi::ITexture* GetAmbientOcclusion() const { return m_Ao[0]; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		DebugRenderer& GetDebugRenderer() { return m_Debug; }
		const RenderStats& GetStats() const { return m_Stats; }
		EnvironmentMap& GetEnvironment() { return m_Environment; }

	private:
		struct DrawItem;
		struct Pipelines;

		void EnsureTargets(uint32_t width, uint32_t height);
		void EnsureShadowMaps(uint32_t sunMapSize);
		void CreatePipelines();
		void RebuildBindingSets();

		void GatherDraws(Scene& scene, const RenderCamera& camera);
		void GatherLights(Scene& scene, const RenderCamera& camera, const SceneRenderOptions& options);
		void PrepareGpuResources(nvrhi::ICommandList* commandList);
		void FillFrameData(Scene& scene, const RenderCamera& camera, const SceneRenderOptions& options);
		void ComputeCascades(const RenderCamera& camera, const glm::vec3& towardsLight, float maxDistance, float lambda, uint32_t cascadeCount, uint32_t mapSize);

		void RenderSunShadows(nvrhi::ICommandList* commandList, uint32_t cascadeCount);
		void RenderLocalShadows(nvrhi::ICommandList* commandList);
		void RenderShadowLayer(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, uint32_t size, const glm::mat4& lightViewProj);
		void RenderPrepass(nvrhi::ICommandList* commandList);
		void RenderAmbientOcclusion(nvrhi::ICommandList* commandList);
		void RenderForward(nvrhi::ICommandList* commandList, const Scene& scene);
		void RenderTonemap(nvrhi::ICommandList* commandList, Tonemapper mode);
		void RenderOverlays(nvrhi::ICommandList* commandList, const SceneRenderOptions& options, const RenderCamera& camera);

		void SetViewport(nvrhi::GraphicsState& state, uint32_t width, uint32_t height) const;

		RenderContext& m_Context;
		EnvironmentMap m_Environment;
		DebugRenderer m_Debug;
		RenderStats m_Stats;

		uint32_t m_Width = 0, m_Height = 0;
		uint32_t m_SunMapSize = 0;

		// Targets
		nvrhi::TextureHandle m_Hdr, m_Depth, m_Normal, m_Ao[2], m_Ldr, m_OutlineMask;
		nvrhi::TextureHandle m_SunShadow, m_LocalShadow;
		nvrhi::FramebufferHandle m_FbPrepass, m_FbForward, m_FbAo[2], m_FbLdr, m_FbLdrDepth, m_FbOutline;
		std::vector<nvrhi::FramebufferHandle> m_FbSunShadow, m_FbLocalShadow;

		// Buffers and binding sets
		nvrhi::BufferHandle m_FrameBuffer, m_LightBuffer, m_DebugVertexBuffer;
		size_t m_DebugVertexCapacity = 0;
		nvrhi::BindingSetHandle m_GlobalSet, m_SceneSet, m_ShadowSet, m_SkySet, m_AoSet, m_BlurSetH, m_BlurSetV, m_TonemapSet, m_OutlineSet;
		nvrhi::BindingLayoutHandle m_ShadowLayout, m_SceneLayout, m_SkyLayout, m_AoLayout, m_BlurLayout, m_TonemapLayout, m_OutlineLayout;
		nvrhi::InputLayoutHandle m_MeshInputLayout, m_LineInputLayout;
		Scope<Pipelines> m_Pipelines;

		// Per-frame state
		FrameData m_Frame{};
		std::vector<DrawItem> m_Opaque, m_Transparent;
		std::vector<LocalLightGPU> m_LocalLights;
		struct ShadowedLocal { uint32_t Layer; uint32_t Faces; glm::mat4 ViewProj[6]; };
		std::vector<ShadowedLocal> m_LocalShadowJobs;
		bool m_HasSun = false;
		bool m_SunCastsShadow = false;
		uint32_t m_CascadeCount = 0;
		glm::mat4 m_CascadeViewProj[MaxCascades];
		uint32_t m_ShadowLayersUsed = 0;
	};

}
