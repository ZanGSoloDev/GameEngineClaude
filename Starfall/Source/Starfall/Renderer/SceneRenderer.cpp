#include "Starfall/Renderer/SceneRenderer.h"

#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Scene/Entity.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Starfall {

	struct SceneRenderer::DrawItem
	{
		Ref<Mesh> MeshRef;
		Ref<Material> MaterialRef;
		glm::mat4 Model;
		glm::mat3 NormalMatrix;
		Math::AABB WorldBounds;
		UUID Entity = UUID(0);
		float Distance = 0.0f;
		bool CastShadows = true;
		bool FlipsWinding = false;
		nvrhi::IBindingSet* MaterialSet = nullptr;
	};

	struct SceneRenderer::Pipelines
	{
		nvrhi::GraphicsPipelineHandle Shadow, ShadowMasked;
		nvrhi::GraphicsPipelineHandle Prepass[2];         // [doubleSided]
		nvrhi::GraphicsPipelineHandle Forward[2];
		nvrhi::GraphicsPipelineHandle Transparent[2];
		nvrhi::GraphicsPipelineHandle Sky, Ssao, SsaoBlur, Tonemap;
		nvrhi::GraphicsPipelineHandle Grid, LinesDepth, LinesOverlay, OutlineMask, OutlineComposite;
	};

	namespace {

		constexpr float ShadowDepthBiasSlope = 1.75f;

		nvrhi::TextureHandle CreateTarget(nvrhi::IDevice* device, uint32_t width, uint32_t height, nvrhi::Format format, const char* name, uint32_t arraySize = 1)
		{
			nvrhi::TextureDesc desc;
			desc.width = width;
			desc.height = height;
			desc.arraySize = arraySize;
			desc.dimension = arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
			desc.format = format;
			desc.isRenderTarget = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			desc.debugName = name;
			return device->createTexture(desc);
		}

		nvrhi::RenderState MakeRenderState(nvrhi::RasterCullMode cull, bool depthTest, bool depthWrite, nvrhi::ComparisonFunc depthFunc, bool blend)
		{
			nvrhi::RenderState state;
			state.rasterState.setCullMode(cull).setFrontCounterClockwise(true);
			state.depthStencilState.setDepthTestEnable(depthTest).setDepthWriteEnable(depthWrite).setDepthFunc(depthFunc).setStencilEnable(false);
			if(blend)
			{
				state.blendState.targets[0].setBlendEnable(true)
					.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
					.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
			}
			return state;
		}

		// Pipelines using the global layout declare 128 bytes of push constants; fullscreen passes only fill a vec4.
		void SetSmallPush(nvrhi::ICommandList* commandList, const glm::vec4* data)
		{
			MeshPushConstants push{};
			if(data)
				push.Model[0] = *data;
			commandList->setPushConstants(&push, sizeof(push));
		}

		glm::mat3 NormalMatrixOf(const glm::mat4& model)
		{
			glm::mat3 m(model);
			float det = glm::determinant(m);
			if(std::abs(det) < 1e-12f)
				return glm::mat3(1.0f);
			return glm::transpose(glm::inverse(m));
		}

		bool SphereInFrustum(const Math::Frustum& frustum, const glm::vec3& center, float radius)
		{
			for(const glm::vec4& plane : frustum.Planes)
				if(glm::dot(glm::vec3(plane), center) + plane.w < -radius)
					return false;
			return true;
		}

		MeshPushConstants MakePush(const glm::mat4& model, const glm::mat3& normal, bool flips)
		{
			MeshPushConstants push;
			push.Model = model;
			push.NormalColumn0 = glm::vec4(normal[0], 0.0f);
			push.NormalColumn1 = glm::vec4(normal[1], 0.0f);
			push.NormalColumn2 = glm::vec4(normal[2], 0.0f);
			push.Params = glm::vec4(flips ? 1.0f : 0.0f, 0, 0, 0);
			return push;
		}

	}

	SceneRenderer::SceneRenderer(RenderContext& context)
		: m_Context(context), m_Environment(context), m_Pipelines(CreateScope<Pipelines>())
	{
		nvrhi::IDevice* device = context.GetDevice();

		m_FrameBuffer = context.CreateVolatileConstantBuffer(sizeof(FrameData), "FrameData");

		nvrhi::BufferDesc lights;
		lights.byteSize = sizeof(LocalLightGPU) * MaxLocalLights;
		lights.structStride = sizeof(LocalLightGPU);
		lights.initialState = nvrhi::ResourceStates::ShaderResource;
		lights.keepInitialState = true;
		lights.debugName = "Local lights";
		m_LightBuffer = device->createBuffer(lights);

		// Layouts for the individual passes (set 0 is always the global layout; see RenderContext).
		nvrhi::BindingLayoutDesc shadow;
		shadow.setVisibility(nvrhi::ShaderType::Vertex).setBindingOffsets(ZeroBindingOffsets()).setRegisterSpaceAndDescriptorSet(0);
		shadow.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::mat4)));
		m_ShadowLayout = device->createBindingLayout(shadow);

		nvrhi::BindingLayoutDesc scene;
		scene.setVisibility(nvrhi::ShaderType::Pixel).setBindingOffsets(ZeroBindingOffsets()).setRegisterSpaceAndDescriptorSet(1);
		scene.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0));
		for(uint32_t i = 1; i <= 6; i++)
			scene.addItem(nvrhi::BindingLayoutItem::Texture_SRV(i));
		m_SceneLayout = device->createBindingLayout(scene);

		auto singleTexture = [&](uint32_t count) {
			nvrhi::BindingLayoutDesc desc;
			desc.setVisibility(nvrhi::ShaderType::Pixel).setBindingOffsets(ZeroBindingOffsets()).setRegisterSpaceAndDescriptorSet(1);
			for(uint32_t i = 0; i < count; i++)
				desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(i));
			return device->createBindingLayout(desc);
		};
		m_SkyLayout = singleTexture(1);
		m_AoLayout = singleTexture(2);
		m_BlurLayout = singleTexture(2);
		m_TonemapLayout = singleTexture(1);
		m_OutlineLayout = singleTexture(1);

		nvrhi::VertexAttributeDesc mesh[4];
		mesh[0].setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex));
		mesh[1].setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Normal)).setElementStride(sizeof(Vertex));
		mesh[2].setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(Vertex, Tangent)).setElementStride(sizeof(Vertex));
		mesh[3].setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, UV)).setElementStride(sizeof(Vertex));
		m_MeshInputLayout = device->createInputLayout(mesh, 4, m_Context.GetShader("pbr.vert"));

		nvrhi::VertexAttributeDesc line[2];
		line[0].setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(DebugVertex, Position)).setElementStride(sizeof(DebugVertex));
		line[1].setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(offsetof(DebugVertex, Color)).setElementStride(sizeof(DebugVertex));
		m_LineInputLayout = device->createInputLayout(line, 2, m_Context.GetShader("line.vert"));
	}

	SceneRenderer::~SceneRenderer()
	{
		m_Context.GetGraphicsDevice().WaitIdle();
	}

	void SceneRenderer::EnsureShadowMaps(uint32_t sunMapSize)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();
		bool rebuild = false;
		if(!m_SunShadow || m_SunMapSize != sunMapSize)
		{
			m_SunMapSize = sunMapSize;
			m_SunShadow = CreateTarget(device, sunMapSize, sunMapSize, nvrhi::Format::D32, "Sun shadow cascades", MaxCascades);
			m_FbSunShadow.clear();
			for(uint32_t i = 0; i < MaxCascades; i++)
				m_FbSunShadow.push_back(device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
					nvrhi::FramebufferAttachment().setTexture(m_SunShadow).setSubresources(nvrhi::TextureSubresourceSet(0, 1, i, 1)))));
			rebuild = true;
		}
		if(!m_LocalShadow)
		{
			m_LocalShadow = CreateTarget(device, LocalShadowMapSize, LocalShadowMapSize, nvrhi::Format::D32, "Local shadow maps", MaxLocalShadowLayers);
			m_FbLocalShadow.clear();
			for(uint32_t i = 0; i < MaxLocalShadowLayers; i++)
				m_FbLocalShadow.push_back(device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
					nvrhi::FramebufferAttachment().setTexture(m_LocalShadow).setSubresources(nvrhi::TextureSubresourceSet(0, 1, i, 1)))));
			rebuild = true;
		}
		if(rebuild && m_Hdr)
			RebuildBindingSets();
	}

	void SceneRenderer::EnsureTargets(uint32_t width, uint32_t height)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();
		bool first = !m_Hdr;
		if(m_Hdr && m_Width == width && m_Height == height)
			return;
		m_Context.GetGraphicsDevice().WaitIdle();
		m_Width = width;
		m_Height = height;

		m_Hdr = CreateTarget(device, width, height, nvrhi::Format::RGBA16_FLOAT, "HDR color");
		m_Depth = CreateTarget(device, width, height, nvrhi::Format::D32, "Scene depth");
		m_Normal = CreateTarget(device, width, height, nvrhi::Format::RGBA16_FLOAT, "View normals");
		m_Ao[0] = CreateTarget(device, width, height, nvrhi::Format::R8_UNORM, "AO 0");
		m_Ao[1] = CreateTarget(device, width, height, nvrhi::Format::R8_UNORM, "AO 1");
		m_Ldr = CreateTarget(device, width, height, nvrhi::Format::RGBA8_UNORM, "LDR output");
		m_OutlineMask = CreateTarget(device, width, height, nvrhi::Format::R8_UNORM, "Outline mask");

		auto fb = [&](std::initializer_list<nvrhi::ITexture*> colors, nvrhi::ITexture* depth, bool depthReadOnly) {
			nvrhi::FramebufferDesc desc;
			for(nvrhi::ITexture* color : colors)
				desc.addColorAttachment(color);
			if(depth)
				desc.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(depth).setReadOnly(depthReadOnly));
			return device->createFramebuffer(desc);
		};
		m_FbPrepass = fb({ m_Normal }, m_Depth, false);
		m_FbForward = fb({ m_Hdr }, m_Depth, true);
		m_FbAo[0] = fb({ m_Ao[0] }, nullptr, false);
		m_FbAo[1] = fb({ m_Ao[1] }, nullptr, false);
		m_FbLdr = fb({ m_Ldr }, nullptr, false);
		m_FbLdrDepth = fb({ m_Ldr }, m_Depth, true);
		m_FbOutline = fb({ m_OutlineMask }, nullptr, false);

		EnsureShadowMaps(m_SunMapSize ? m_SunMapSize : 2048);
		if(first || !m_Pipelines->Forward[0])
			CreatePipelines();
		RebuildBindingSets();
	}

	void SceneRenderer::RebuildBindingSets()
	{
		nvrhi::IDevice* device = m_Context.GetDevice();

		nvrhi::BindingSetDesc global;
		global.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameBuffer));
		global.addItem(nvrhi::BindingSetItem::Sampler(1, m_Context.GetLinearClampSampler()));
		global.addItem(nvrhi::BindingSetItem::Sampler(2, m_Context.GetShadowCompareSampler()));
		global.addItem(nvrhi::BindingSetItem::Sampler(3, m_Context.GetPointClampSampler()));
		global.addItem(nvrhi::BindingSetItem::PushConstants(4, sizeof(MeshPushConstants)));
		m_GlobalSet = device->createBindingSet(global, m_Context.GetGlobalLayout());

		nvrhi::BindingSetDesc shadow;
		shadow.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::mat4)));
		m_ShadowSet = device->createBindingSet(shadow, m_ShadowLayout);

		auto cubeSRV = [](uint32_t slot, nvrhi::ITexture* t) {
			return nvrhi::BindingSetItem::Texture_SRV(slot, t, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube);
		};
		auto arraySRV = [](uint32_t slot, nvrhi::ITexture* t) {
			return nvrhi::BindingSetItem::Texture_SRV(slot, t, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray);
		};

		nvrhi::BindingSetDesc scene;
		scene.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_LightBuffer));
		scene.addItem(arraySRV(1, m_SunShadow));
		scene.addItem(arraySRV(2, m_LocalShadow));
		scene.addItem(cubeSRV(3, m_Environment.GetIrradiance()));
		scene.addItem(cubeSRV(4, m_Environment.GetPrefiltered()));
		scene.addItem(nvrhi::BindingSetItem::Texture_SRV(5, m_Context.GetBrdfLut()));
		scene.addItem(nvrhi::BindingSetItem::Texture_SRV(6, m_Ao[0]));
		m_SceneSet = device->createBindingSet(scene, m_SceneLayout);

		nvrhi::BindingSetDesc sky;
		sky.addItem(cubeSRV(0, m_Environment.GetEnvironment()));
		m_SkySet = device->createBindingSet(sky, m_SkyLayout);

		auto two = [&](nvrhi::ITexture* a, nvrhi::ITexture* b, nvrhi::IBindingLayout* layout) {
			nvrhi::BindingSetDesc desc;
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, a));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, b));
			return device->createBindingSet(desc, layout);
		};
		auto one = [&](nvrhi::ITexture* a, nvrhi::IBindingLayout* layout) {
			nvrhi::BindingSetDesc desc;
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, a));
			return device->createBindingSet(desc, layout);
		};
		m_AoSet = two(m_Depth, m_Normal, m_AoLayout);
		m_BlurSetH = two(m_Ao[0], m_Depth, m_BlurLayout);
		m_BlurSetV = two(m_Ao[1], m_Depth, m_BlurLayout);
		m_TonemapSet = one(m_Hdr, m_TonemapLayout);
		m_OutlineSet = one(m_OutlineMask, m_OutlineLayout);
	}

	void SceneRenderer::CreatePipelines()
	{
		nvrhi::IDevice* device = m_Context.GetDevice();
		Pipelines& p = *m_Pipelines;
		using Cull = nvrhi::RasterCullMode;
		nvrhi::IBindingLayout* global = m_Context.GetGlobalLayout();
		nvrhi::IBindingLayout* material = m_Context.GetMaterialLayout();

		auto make = [&](nvrhi::IShader* vs, nvrhi::IShader* ps, std::initializer_list<nvrhi::IBindingLayout*> layouts, nvrhi::IFramebuffer* fb, const nvrhi::RenderState& state,
			nvrhi::IInputLayout* input, nvrhi::PrimitiveType prim = nvrhi::PrimitiveType::TriangleList) -> nvrhi::GraphicsPipelineHandle {
			if(!vs)
				return nullptr;
			nvrhi::GraphicsPipelineDesc desc;
			desc.setPrimType(prim).setInputLayout(input).setVertexShader(vs).setRenderState(state);
			if(ps)
				desc.setPixelShader(ps);
			for(nvrhi::IBindingLayout* layout : layouts)
				desc.addBindingLayout(layout);
			return device->createGraphicsPipeline(desc, fb);
		};

		// Shadow casters (depth only, back faces rendered with slope-scaled bias).
		nvrhi::RenderState shadowState = MakeRenderState(Cull::None, true, true, nvrhi::ComparisonFunc::Less, false);
		shadowState.rasterState.setDepthBias(2).setSlopeScaleDepthBias(ShadowDepthBiasSlope);
		p.Shadow = make(m_Context.GetShader("shadow.vert"), nullptr, { m_ShadowLayout }, m_FbSunShadow[0], shadowState, m_MeshInputLayout);
		p.ShadowMasked = make(m_Context.GetShader("shadow.vert"), m_Context.GetShader("shadow_mask.frag"), { m_ShadowLayout, material }, m_FbSunShadow[0], shadowState, m_MeshInputLayout);

		for(int doubleSided = 0; doubleSided < 2; doubleSided++)
		{
			Cull cull = doubleSided ? Cull::None : Cull::Back;
			p.Prepass[doubleSided] = make(m_Context.GetShader("prepass.vert"), m_Context.GetShader("prepass.frag"), { global, material }, m_FbPrepass,
				MakeRenderState(cull, true, true, nvrhi::ComparisonFunc::Less, false), m_MeshInputLayout);
			p.Forward[doubleSided] = make(m_Context.GetShader("pbr.vert"), m_Context.GetShader("pbr.frag"), { global, m_SceneLayout, material }, m_FbForward,
				MakeRenderState(cull, true, false, nvrhi::ComparisonFunc::LessOrEqual, false), m_MeshInputLayout);
			p.Transparent[doubleSided] = make(m_Context.GetShader("pbr.vert"), m_Context.GetShader("pbr.frag"), { global, m_SceneLayout, material }, m_FbForward,
				MakeRenderState(cull, true, false, nvrhi::ComparisonFunc::LessOrEqual, true), m_MeshInputLayout);
		}

		nvrhi::RenderState fullscreen = MakeRenderState(Cull::None, false, false, nvrhi::ComparisonFunc::Always, false);
		p.Sky = make(m_Context.GetShader("sky.vert"), m_Context.GetShader("sky.frag"), { global, m_SkyLayout }, m_FbForward,
			MakeRenderState(Cull::None, true, false, nvrhi::ComparisonFunc::LessOrEqual, false), nullptr);
		p.Ssao = make(m_Context.GetShader("fullscreen.vert"), m_Context.GetShader("ssao.frag"), { global, m_AoLayout }, m_FbAo[0], fullscreen, nullptr);
		p.SsaoBlur = make(m_Context.GetShader("fullscreen.vert"), m_Context.GetShader("ssao_blur.frag"), { global, m_BlurLayout }, m_FbAo[0], fullscreen, nullptr);
		p.Tonemap = make(m_Context.GetShader("fullscreen.vert"), m_Context.GetShader("tonemap.frag"), { global, m_TonemapLayout }, m_FbLdr, fullscreen, nullptr);

		p.Grid = make(m_Context.GetShader("grid.vert"), m_Context.GetShader("grid.frag"), { global }, m_FbLdrDepth,
			MakeRenderState(Cull::None, true, false, nvrhi::ComparisonFunc::LessOrEqual, true), nullptr);
		p.LinesDepth = make(m_Context.GetShader("line.vert"), m_Context.GetShader("line.frag"), { global }, m_FbLdrDepth,
			MakeRenderState(Cull::None, true, false, nvrhi::ComparisonFunc::LessOrEqual, true), m_LineInputLayout, nvrhi::PrimitiveType::LineList);
		p.LinesOverlay = make(m_Context.GetShader("line.vert"), m_Context.GetShader("line.frag"), { global }, m_FbLdrDepth,
			MakeRenderState(Cull::None, false, false, nvrhi::ComparisonFunc::Always, true), m_LineInputLayout, nvrhi::PrimitiveType::LineList);
		p.OutlineMask = make(m_Context.GetShader("outline_mask.vert"), m_Context.GetShader("outline_mask.frag"), { global }, m_FbOutline,
			MakeRenderState(Cull::None, false, false, nvrhi::ComparisonFunc::Always, false), m_MeshInputLayout);
		p.OutlineComposite = make(m_Context.GetShader("fullscreen.vert"), m_Context.GetShader("outline.frag"), { global, m_OutlineLayout }, m_FbLdr,
			MakeRenderState(Cull::None, false, false, nvrhi::ComparisonFunc::Always, true), nullptr);

		const std::pair<const char*, nvrhi::IGraphicsPipeline*> all[] = {
			{ "Shadow", p.Shadow }, { "ShadowMasked", p.ShadowMasked }, { "Prepass", p.Prepass[0] }, { "PrepassDS", p.Prepass[1] }, { "Forward", p.Forward[0] },
			{ "ForwardDS", p.Forward[1] }, { "Transparent", p.Transparent[0] }, { "TransparentDS", p.Transparent[1] }, { "Sky", p.Sky }, { "Ssao", p.Ssao },
			{ "SsaoBlur", p.SsaoBlur }, { "Tonemap", p.Tonemap }, { "Grid", p.Grid }, { "LinesDepth", p.LinesDepth }, { "LinesOverlay", p.LinesOverlay },
			{ "OutlineMask", p.OutlineMask }, { "OutlineComposite", p.OutlineComposite } };
		for(const auto& [name, pipeline] : all)
			if(!pipeline)
				SF_CORE_ERROR("Failed to create the '{0}' pipeline", name);
	}

	void SceneRenderer::SetViewport(nvrhi::GraphicsState& state, uint32_t width, uint32_t height) const
	{
		state.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height))));
	}

	void SceneRenderer::GatherDraws(Scene& scene, const RenderCamera& camera)
	{
		m_Opaque.clear();
		m_Transparent.clear();
		Math::Frustum frustum = Math::Frustum::FromViewProjection(camera.Projection * camera.View);

		scene.Each<MeshRendererComponent, TransformComponent>([&](Entity entity, MeshRendererComponent& mr, TransformComponent&) {
			if(!mr.Visible || mr.Mesh.empty())
				return;
			Ref<Mesh> mesh = AssetManager::GetMesh(mr.Mesh);
			if(!mesh || mesh->GetIndexCount() == 0)
				return;
			Ref<Material> material = mr.Material.empty() ? mesh->GetDefaultMaterial() : AssetManager::GetMaterial(mr.Material);
			if(!material)
				material = AssetManager::GetDefaultMaterial();

			DrawItem item;
			item.Model = scene.GetWorldTransform(entity);
			item.WorldBounds = mesh->GetBounds().Transformed(item.Model);
			if(!frustum.Intersects(item.WorldBounds))
			{
				m_Stats.MeshesCulled++;
				// Shadow casters outside the view can still cast into it; keep them in a separate pass only if they cast.
				if(!mr.CastShadows)
					return;
				item.MeshRef = mesh;
				item.MaterialRef = material;
				item.NormalMatrix = NormalMatrixOf(item.Model);
				item.Entity = entity.GetUUID();
				item.CastShadows = true;
				item.Distance = -1.0f; // marks "shadow only"
				m_Opaque.push_back(std::move(item));
				return;
			}
			item.MeshRef = mesh;
			item.MaterialRef = material;
			item.NormalMatrix = NormalMatrixOf(item.Model);
			item.FlipsWinding = glm::determinant(glm::mat3(item.Model)) < 0.0f;
			item.Entity = entity.GetUUID();
			item.CastShadows = mr.CastShadows;
			item.Distance = glm::distance(glm::vec3(item.Model[3]), camera.Position);
			m_Stats.MeshesSubmitted++;
			if(material->GetData().Alpha == AlphaMode::Blend)
				m_Transparent.push_back(std::move(item));
			else
				m_Opaque.push_back(std::move(item));
		});

		std::sort(m_Opaque.begin(), m_Opaque.end(), [](const DrawItem& a, const DrawItem& b) {
			if(a.MaterialRef != b.MaterialRef)
				return a.MaterialRef < b.MaterialRef;
			if(a.MeshRef != b.MeshRef)
				return a.MeshRef < b.MeshRef;
			return a.Distance < b.Distance;
		});
		std::sort(m_Transparent.begin(), m_Transparent.end(), [](const DrawItem& a, const DrawItem& b) { return a.Distance > b.Distance; });
	}

	void SceneRenderer::PrepareGpuResources(nvrhi::ICommandList* commandList)
	{
		for(auto* list : { &m_Opaque, &m_Transparent })
		{
			for(DrawItem& item : *list)
			{
				m_Context.PrepareMesh(commandList, *item.MeshRef);
				item.MaterialSet = m_Context.PrepareMaterial(commandList, *item.MaterialRef);
			}
		}
	}

	void SceneRenderer::GatherLights(Scene& scene, const RenderCamera& camera, const SceneRenderOptions& options)
	{
		m_LocalLights.clear();
		m_LocalShadowJobs.clear();
		m_ShadowLayersUsed = 0;
		m_HasSun = false;
		m_SunCastsShadow = false;

		Math::Frustum frustum = Math::Frustum::FromViewProjection(camera.Projection * camera.View);

		struct Candidate
		{
			LocalLightGPU Gpu;
			LightComponent Light;
			glm::vec3 Position;
			glm::vec3 Forward;
			float Priority;
		};
		std::vector<Candidate> locals;

		float bestSun = -1.0f;
		LightComponent sun;
		glm::vec3 sunDirection(0, 1, 0);

		scene.Each<LightComponent, TransformComponent>([&](Entity entity, LightComponent& light, TransformComponent&) {
			glm::mat4 world = scene.GetWorldTransform(entity);
			glm::vec3 position(world[3]);
			glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
			float luminance = glm::dot(light.Color, glm::vec3(0.3f, 0.59f, 0.11f)) * light.Intensity;

			if(light.Type == LightType::Directional)
			{
				if(luminance > bestSun)
				{
					bestSun = luminance;
					sun = light;
					sunDirection = -forward;
				}
				return;
			}

			float range = std::max(light.Range, 0.01f);
			if(!SphereInFrustum(frustum, position, range) || light.Intensity <= 0.0f)
				return;

			Candidate c;
			c.Light = light;
			c.Position = position;
			c.Forward = forward;
			c.Gpu.PositionRange = glm::vec4(position, range);
			c.Gpu.ColorIntensity = glm::vec4(light.Color * light.Intensity, 0.0f);
			c.Gpu.DirectionType = glm::vec4(forward, light.Type == LightType::Spot ? 2.0f : 1.0f);
			float outer = std::clamp(light.OuterConeAngle, 0.01f, glm::radians(89.0f));
			float inner = std::clamp(light.InnerConeAngle, 0.0f, outer - 0.001f);
			c.Gpu.SpotParams = glm::vec4(std::cos(inner), std::cos(outer), 0, 0);
			c.Gpu.ShadowParams = glm::vec4(-1.0f, 0.0f, 0.0f, light.ShadowSoftness);
			float dist = glm::distance(position, camera.Position);
			c.Priority = luminance * range / (dist * dist + 1.0f);
			locals.push_back(c);
		});

		if(bestSun > 0.0f)
		{
			m_HasSun = true;
			m_SunCastsShadow = sun.CastShadows && options.EnableShadows;
			m_Frame.SunDirection = glm::vec4(glm::normalize(sunDirection), 1.0f);
			m_Frame.SunColor = glm::vec4(sun.Color * sun.Intensity, sun.ShadowSoftness);
			m_Frame.SunShadowParams.x = sun.ShadowBias;
		}
		else
		{
			m_Frame.SunDirection = glm::vec4(0, 1, 0, 0);
			m_Frame.SunColor = glm::vec4(0.0f);
		}

		std::sort(locals.begin(), locals.end(), [](const Candidate& a, const Candidate& b) { return a.Priority > b.Priority; });
		if(locals.size() > MaxLocalLights)
			locals.resize(MaxLocalLights);

		for(Candidate& c : locals)
		{
			bool wantsShadow = options.EnableShadows && c.Light.CastShadows;
			uint32_t faces = c.Light.Type == LightType::Spot ? 1u : 6u;
			if(wantsShadow && m_ShadowLayersUsed + faces <= MaxLocalShadowLayers)
			{
				ShadowedLocal job;
				job.Layer = m_ShadowLayersUsed;
				job.Faces = faces;
				float range = std::max(c.Light.Range, 0.01f);
				float nearPlane = std::max(0.05f, range * 0.002f);
				float texelPerUnit;
				if(faces == 1)
				{
					float fov = std::clamp(c.Light.OuterConeAngle * 2.0f + glm::radians(4.0f), glm::radians(10.0f), glm::radians(170.0f));
					glm::vec3 up = std::abs(c.Forward.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
					job.ViewProj[0] = glm::perspective(fov, 1.0f, nearPlane, range) * glm::lookAt(c.Position, c.Position + c.Forward, up);
					texelPerUnit = 2.0f * std::tan(fov * 0.5f) / static_cast<float>(LocalShadowMapSize);
				}
				else
				{
					static const glm::vec3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
					static const glm::vec3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0, 1, 0 } };
					float fov = glm::radians(92.0f);
					glm::mat4 proj = glm::perspective(fov, 1.0f, nearPlane, range);
					for(uint32_t f = 0; f < 6; f++)
						job.ViewProj[f] = proj * glm::lookAt(c.Position, c.Position + dirs[f], ups[f]);
					texelPerUnit = 2.0f * std::tan(fov * 0.5f) / static_cast<float>(LocalShadowMapSize);
				}
				c.Gpu.ShadowParams = glm::vec4(static_cast<float>(job.Layer), texelPerUnit, c.Light.ShadowBias * 0.05f, c.Light.ShadowSoftness);
				for(uint32_t f = 0; f < faces; f++)
					m_Frame.LocalShadowViewProj[job.Layer + f] = job.ViewProj[f];
				m_ShadowLayersUsed += faces;
				m_LocalShadowJobs.push_back(job);
			}
			m_LocalLights.push_back(c.Gpu);
		}
		m_Stats.LocalLights = static_cast<uint32_t>(m_LocalLights.size());
		m_Stats.ShadowedLocalLights = static_cast<uint32_t>(m_LocalShadowJobs.size());
	}

	void SceneRenderer::ComputeCascades(const RenderCamera& camera, const glm::vec3& towardsLight, float maxDistance, float lambda, uint32_t cascadeCount, uint32_t mapSize)
	{
		float nearPlane = std::max(camera.Near, 0.01f);
		float farPlane = std::min(camera.Far, maxDistance);
		farPlane = std::max(farPlane, nearPlane + 0.1f);

		// Practical split scheme: blend of logarithmic and uniform splits.
		float splits[MaxCascades + 1];
		splits[0] = nearPlane;
		for(uint32_t i = 1; i <= cascadeCount; i++)
		{
			float t = static_cast<float>(i) / static_cast<float>(cascadeCount);
			float logarithmic = nearPlane * std::pow(farPlane / nearPlane, t);
			float uniform = nearPlane + (farPlane - nearPlane) * t;
			splits[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
		}

		glm::mat4 invView = glm::inverse(camera.View);
		float tanHalfY = 1.0f / camera.Projection[1][1];
		float tanHalfX = 1.0f / camera.Projection[0][0];
		glm::vec3 up = std::abs(towardsLight.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

		for(uint32_t c = 0; c < cascadeCount; c++)
		{
			glm::vec3 corners[8];
			for(int i = 0; i < 8; i++)
			{
				float d = (i & 4) ? splits[c + 1] : splits[c];
				float sx = (i & 1) ? 1.0f : -1.0f;
				float sy = (i & 2) ? 1.0f : -1.0f;
				glm::vec3 view = camera.Orthographic ? glm::vec3(sx * tanHalfX, sy * tanHalfY, -d) : glm::vec3(sx * tanHalfX * d, sy * tanHalfY * d, -d);
				corners[i] = glm::vec3(invView * glm::vec4(view, 1.0f));
			}
			glm::vec3 center(0.0f);
			for(const glm::vec3& corner : corners)
				center += corner;
			center /= 8.0f;
			float radius = 0.0f;
			for(const glm::vec3& corner : corners)
				radius = std::max(radius, glm::distance(corner, center));
			radius = std::ceil(radius * 16.0f) / 16.0f; // stable size to avoid shimmering when the camera rotates

			// Snap the light-space center to texel increments so the shadow map does not swim when the camera moves.
			glm::mat4 lightView = glm::lookAt(center + towardsLight * radius * 2.0f, center, up);
			float worldPerTexel = (radius * 2.0f) / static_cast<float>(mapSize);
			glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));
			centerLS.x = std::floor(centerLS.x / worldPerTexel) * worldPerTexel;
			centerLS.y = std::floor(centerLS.y / worldPerTexel) * worldPerTexel;
			glm::vec3 snappedCenter = glm::vec3(glm::inverse(lightView) * glm::vec4(centerLS, 1.0f));
			lightView = glm::lookAt(snappedCenter + towardsLight * radius * 2.0f, snappedCenter, up);

			glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
			m_CascadeViewProj[c] = lightProj * lightView;
			m_Frame.CascadeViewProj[c] = m_CascadeViewProj[c];
			m_Frame.CascadeWorldSize[static_cast<int>(c)] = radius * 2.0f;
			m_Frame.CascadeDepthRange[static_cast<int>(c)] = radius * 4.0f;
			m_Frame.CascadeSplits[static_cast<int>(c)] = splits[c + 1];
		}
	}

	void SceneRenderer::FillFrameData(Scene& scene, const RenderCamera& camera, const SceneRenderOptions& options)
	{
		const SceneSettings& settings = scene.GetSettings();
		glm::mat4 viewProj = camera.Projection * camera.View;

		// Sun fields are written by GatherLights (preserve them).
		glm::vec4 sunDirection = m_Frame.SunDirection, sunColor = m_Frame.SunColor, sunShadow = m_Frame.SunShadowParams;
		glm::mat4 localShadow[MaxLocalShadowLayers];
		std::copy(std::begin(m_Frame.LocalShadowViewProj), std::end(m_Frame.LocalShadowViewProj), std::begin(localShadow));

		m_Frame = FrameData{};
		m_Frame.View = camera.View;
		m_Frame.Proj = camera.Projection;
		m_Frame.ViewProj = viewProj;
		m_Frame.InvView = glm::inverse(camera.View);
		m_Frame.InvProj = glm::inverse(camera.Projection);
		m_Frame.InvViewProj = glm::inverse(viewProj);
		m_Frame.CameraPosition = glm::vec4(camera.Position, camera.Near);
		m_Frame.ScreenParams = glm::vec4(static_cast<float>(m_Width), static_cast<float>(m_Height), 1.0f / static_cast<float>(m_Width), 1.0f / static_cast<float>(m_Height));
		m_Frame.TimeParams = glm::vec4(options.Time, options.DeltaTime, static_cast<float>(options.Frame), camera.Far);
		m_Frame.EnvParams = glm::vec4(settings.Environment.Intensity, m_Environment.GetMaxPrefilterLod(), settings.PostProcess.Exposure, glm::radians(settings.Environment.RotationDegrees));
		m_Frame.SkyParams = glm::vec4(settings.Environment.ShowSkybox ? 1.0f : 0.0f, 0.0f, 0, 0);
		m_Frame.SunDirection = sunDirection;
		m_Frame.SunColor = sunColor;
		m_Frame.SunShadowParams = sunShadow;
		std::copy(std::begin(localShadow), std::end(localShadow), std::begin(m_Frame.LocalShadowViewProj));
		m_Frame.LightCounts = glm::ivec4(static_cast<int>(m_LocalLights.size()), 0, 0, 0);
		bool ssao = options.EnableSSAO && settings.PostProcess.SSAOEnabled;
		m_Frame.SSAOParams = glm::vec4(settings.PostProcess.SSAORadius, settings.PostProcess.SSAOIntensity, static_cast<float>(settings.PostProcess.SSAOSamples), ssao ? 1.0f : 0.0f);

		m_CascadeCount = 0;
		if(m_HasSun && m_SunCastsShadow)
		{
			uint32_t mapSize = std::clamp(settings.Shadows.MapSize, 256u, 8192u);
			EnsureShadowMaps(mapSize);
			m_CascadeCount = std::clamp(settings.Shadows.CascadeCount, 1u, MaxCascades);
			ComputeCascades(camera, glm::normalize(glm::vec3(sunDirection)), settings.Shadows.MaxDistance, settings.Shadows.CascadeSplitLambda, m_CascadeCount, mapSize);
			m_Frame.SunShadowParams.y = static_cast<float>(m_CascadeCount);
			m_Frame.SunShadowParams.z = static_cast<float>(mapSize);
			m_Frame.SunShadowParams.w = 1.0f;
		}
		m_Stats.Cascades = m_CascadeCount;
	}

	void SceneRenderer::RenderShadowLayer(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, uint32_t size, const glm::mat4& lightViewProj)
	{
		Math::Frustum frustum = Math::Frustum::FromViewProjection(lightViewProj);
		nvrhi::ITexture* target = framebuffer->getDesc().depthAttachment.texture;
		commandList->clearDepthStencilTexture(target, framebuffer->getDesc().depthAttachment.subresources, true, 1.0f, false, 0);

		nvrhi::GraphicsState state;
		state.setFramebuffer(framebuffer);
		SetViewport(state, size, size);
		state.addBindingSet(m_ShadowSet);

		for(const DrawItem& item : m_Opaque)
		{
			if(!item.CastShadows || !frustum.Intersects(item.WorldBounds))
				continue;
			const bool masked = item.MaterialRef->GetData().Alpha == AlphaMode::Mask;
			state.setPipeline(masked ? m_Pipelines->ShadowMasked : m_Pipelines->Shadow);
			state.bindings.resize(1);
			if(masked)
				state.addBindingSet(item.MaterialSet);
			state.vertexBuffers.resize(0);
			state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.MeshRef->GetVertexBuffer()).setSlot(0));
			state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.MeshRef->GetIndexBuffer()).setFormat(nvrhi::Format::R32_UINT));
			commandList->setGraphicsState(state);
			glm::mat4 mvp = lightViewProj * item.Model;
			commandList->setPushConstants(&mvp, sizeof(mvp));
			commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(item.MeshRef->GetIndexCount()));
			m_Stats.ShadowDrawCalls++;
		}
	}

	void SceneRenderer::RenderSunShadows(nvrhi::ICommandList* commandList, uint32_t cascadeCount)
	{
		for(uint32_t c = 0; c < cascadeCount; c++)
			RenderShadowLayer(commandList, m_FbSunShadow[c], m_SunMapSize, m_CascadeViewProj[c]);
	}

	void SceneRenderer::RenderLocalShadows(nvrhi::ICommandList* commandList)
	{
		for(const ShadowedLocal& job : m_LocalShadowJobs)
			for(uint32_t f = 0; f < job.Faces; f++)
				RenderShadowLayer(commandList, m_FbLocalShadow[job.Layer + f], LocalShadowMapSize, job.ViewProj[f]);
	}

	void SceneRenderer::RenderPrepass(nvrhi::ICommandList* commandList)
	{
		commandList->clearDepthStencilTexture(m_Depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
		commandList->clearTextureFloat(m_Normal, nvrhi::AllSubresources, nvrhi::Color(0.5f, 0.5f, 1.0f, 1.0f));

		nvrhi::GraphicsState state;
		state.setFramebuffer(m_FbPrepass);
		SetViewport(state, m_Width, m_Height);
		for(const DrawItem& item : m_Opaque)
		{
			if(item.Distance < 0.0f)
				continue; // shadow-only
			bool doubleSided = item.MaterialRef->GetData().DoubleSided;
			state.setPipeline(m_Pipelines->Prepass[doubleSided ? 1 : 0]);
			state.bindings.resize(0);
			state.addBindingSet(m_GlobalSet);
			state.addBindingSet(item.MaterialSet);
			state.vertexBuffers.resize(0);
			state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.MeshRef->GetVertexBuffer()).setSlot(0));
			state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.MeshRef->GetIndexBuffer()).setFormat(nvrhi::Format::R32_UINT));
			commandList->setGraphicsState(state);
			MeshPushConstants push = MakePush(item.Model, item.NormalMatrix, item.FlipsWinding);
			commandList->setPushConstants(&push, sizeof(push));
			commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(item.MeshRef->GetIndexCount()));
		}
	}

	void SceneRenderer::RenderAmbientOcclusion(nvrhi::ICommandList* commandList)
	{
		auto pass = [&](nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* fb, nvrhi::IBindingSet* inputs, const glm::vec4* push) {
			nvrhi::GraphicsState state;
			state.setPipeline(pipeline).setFramebuffer(fb);
			SetViewport(state, m_Width, m_Height);
			state.addBindingSet(m_GlobalSet);
			state.addBindingSet(inputs);
			commandList->setGraphicsState(state);
			SetSmallPush(commandList, push);
			commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		};
		pass(m_Pipelines->Ssao, m_FbAo[0], m_AoSet, nullptr);
		glm::vec4 horizontal(1, 0, 0, 0), vertical(0, 1, 0, 0);
		pass(m_Pipelines->SsaoBlur, m_FbAo[1], m_BlurSetH, &horizontal);
		pass(m_Pipelines->SsaoBlur, m_FbAo[0], m_BlurSetV, &vertical);
	}

	void SceneRenderer::RenderForward(nvrhi::ICommandList* commandList, const Scene& scene)
	{
		const EnvironmentSettings& env = scene.GetSettings().Environment;
		commandList->clearTextureFloat(m_Hdr, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 1));

		nvrhi::GraphicsState state;
		state.setFramebuffer(m_FbForward);
		SetViewport(state, m_Width, m_Height);

		auto draw = [&](const DrawItem& item, nvrhi::IGraphicsPipeline* pipeline) {
			state.setPipeline(pipeline);
			state.bindings.resize(0);
			state.addBindingSet(m_GlobalSet);
			state.addBindingSet(m_SceneSet);
			state.addBindingSet(item.MaterialSet);
			state.vertexBuffers.resize(0);
			state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.MeshRef->GetVertexBuffer()).setSlot(0));
			state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.MeshRef->GetIndexBuffer()).setFormat(nvrhi::Format::R32_UINT));
			commandList->setGraphicsState(state);
			MeshPushConstants push = MakePush(item.Model, item.NormalMatrix, item.FlipsWinding);
			commandList->setPushConstants(&push, sizeof(push));
			commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(item.MeshRef->GetIndexCount()));
			m_Stats.DrawCalls++;
			m_Stats.Triangles += item.MeshRef->GetIndexCount() / 3;
		};

		for(const DrawItem& item : m_Opaque)
		{
			if(item.Distance < 0.0f)
				continue;
			draw(item, m_Pipelines->Forward[item.MaterialRef->GetData().DoubleSided ? 1 : 0]);
		}

		if(env.ShowSkybox)
		{
			nvrhi::GraphicsState sky;
			sky.setPipeline(m_Pipelines->Sky).setFramebuffer(m_FbForward);
			SetViewport(sky, m_Width, m_Height);
			sky.addBindingSet(m_GlobalSet);
			sky.addBindingSet(m_SkySet);
			commandList->setGraphicsState(sky);
			SetSmallPush(commandList, nullptr);
			commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		}

		for(const DrawItem& item : m_Transparent)
			draw(item, m_Pipelines->Transparent[item.MaterialRef->GetData().DoubleSided ? 1 : 0]);
	}

	void SceneRenderer::RenderTonemap(nvrhi::ICommandList* commandList, Tonemapper mode)
	{
		nvrhi::GraphicsState state;
		state.setPipeline(m_Pipelines->Tonemap).setFramebuffer(m_FbLdr);
		SetViewport(state, m_Width, m_Height);
		state.addBindingSet(m_GlobalSet);
		state.addBindingSet(m_TonemapSet);
		commandList->setGraphicsState(state);
		glm::vec4 push(static_cast<float>(mode), 0, 0, 0);
		SetSmallPush(commandList, &push);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

	void SceneRenderer::RenderOverlays(nvrhi::ICommandList* commandList, const SceneRenderOptions& options, const RenderCamera& camera)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();

		// Selection outline: mask pass + edge composite.
		if(!options.OutlinedEntities.empty())
		{
			std::unordered_set<UUID> selected(options.OutlinedEntities.begin(), options.OutlinedEntities.end());
			commandList->clearTextureFloat(m_OutlineMask, nvrhi::AllSubresources, nvrhi::Color(0.0f));
			nvrhi::GraphicsState state;
			state.setPipeline(m_Pipelines->OutlineMask).setFramebuffer(m_FbOutline);
			SetViewport(state, m_Width, m_Height);
			state.addBindingSet(m_GlobalSet);
			bool any = false;
			for(auto* list : { &m_Opaque, &m_Transparent })
			{
				for(const DrawItem& item : *list)
				{
					if(item.Distance < 0.0f || !selected.contains(item.Entity))
						continue;
					state.vertexBuffers.resize(0);
					state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.MeshRef->GetVertexBuffer()).setSlot(0));
					state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.MeshRef->GetIndexBuffer()).setFormat(nvrhi::Format::R32_UINT));
					commandList->setGraphicsState(state);
					MeshPushConstants push = MakePush(item.Model, item.NormalMatrix, false);
					commandList->setPushConstants(&push, sizeof(push));
					commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(item.MeshRef->GetIndexCount()));
					any = true;
				}
			}
			if(any)
			{
				nvrhi::GraphicsState composite;
				composite.setPipeline(m_Pipelines->OutlineComposite).setFramebuffer(m_FbLdr);
				SetViewport(composite, m_Width, m_Height);
				composite.addBindingSet(m_GlobalSet);
				composite.addBindingSet(m_OutlineSet);
				commandList->setGraphicsState(composite);
				glm::vec4 push(options.OutlineColor.r, options.OutlineColor.g, options.OutlineColor.b, 2.0f);
				SetSmallPush(commandList, &push);
				commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
			}
		}

		if(options.DrawGrid)
		{
			nvrhi::GraphicsState state;
			state.setPipeline(m_Pipelines->Grid).setFramebuffer(m_FbLdrDepth);
			SetViewport(state, m_Width, m_Height);
			state.addBindingSet(m_GlobalSet);
			commandList->setGraphicsState(state);
			float extent = std::min(camera.Far, 400.0f);
			glm::vec4 push(extent, std::round(camera.Position.x), std::round(camera.Position.z), 0);
			SetSmallPush(commandList, &push);
			commandList->draw(nvrhi::DrawArguments().setVertexCount(6));
		}

		// Debug lines: one dynamic vertex buffer holding the depth-tested lines followed by the overlay lines.
		const auto& depthLines = m_Debug.GetDepthTestedVertices();
		const auto& overlayLines = m_Debug.GetOverlayVertices();
		size_t total = depthLines.size() + overlayLines.size();
		if(total > 0)
		{
			if(!m_DebugVertexBuffer || m_DebugVertexCapacity < total)
			{
				m_DebugVertexCapacity = std::max<size_t>(total * 2, 4096);
				nvrhi::BufferDesc desc;
				desc.byteSize = m_DebugVertexCapacity * sizeof(DebugVertex);
				desc.isVertexBuffer = true;
				desc.initialState = nvrhi::ResourceStates::VertexBuffer;
				desc.keepInitialState = true;
				desc.debugName = "Debug lines";
				m_DebugVertexBuffer = device->createBuffer(desc);
			}
			if(!depthLines.empty())
				commandList->writeBuffer(m_DebugVertexBuffer, depthLines.data(), depthLines.size() * sizeof(DebugVertex), 0);
			if(!overlayLines.empty())
				commandList->writeBuffer(m_DebugVertexBuffer, overlayLines.data(), overlayLines.size() * sizeof(DebugVertex), depthLines.size() * sizeof(DebugVertex));

			auto drawLines = [&](nvrhi::IGraphicsPipeline* pipeline, size_t first, size_t count) {
				if(count == 0)
					return;
				nvrhi::GraphicsState state;
				state.setPipeline(pipeline).setFramebuffer(m_FbLdrDepth);
				SetViewport(state, m_Width, m_Height);
				state.addBindingSet(m_GlobalSet);
				state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(m_DebugVertexBuffer).setSlot(0));
				commandList->setGraphicsState(state);
				SetSmallPush(commandList, nullptr);
				commandList->draw(nvrhi::DrawArguments().setVertexCount(static_cast<uint32_t>(count)).setStartVertexLocation(static_cast<uint32_t>(first)));
			};
			drawLines(m_Pipelines->LinesDepth, 0, depthLines.size());
			drawLines(m_Pipelines->LinesOverlay, depthLines.size(), overlayLines.size());
		}
		m_Debug.Clear();
	}

	void SceneRenderer::Render(nvrhi::ICommandList* commandList, Scene& scene, const RenderCamera& camera, uint32_t width, uint32_t height, const SceneRenderOptions& options)
	{
		m_Stats = RenderStats{};
		if(width == 0 || height == 0)
			return;

		EnsureTargets(width, height);
		if(!m_Pipelines->Forward[0] || !m_Pipelines->Tonemap)
		{
			SF_CORE_ERROR("SceneRenderer pipelines are not available; skipping frame");
			return;
		}

		m_Context.EnsureBrdfLut(commandList);
		m_Environment.Update(commandList, scene.GetSettings().Environment);

		m_Frame = FrameData{};
		GatherDraws(scene, camera);
		GatherLights(scene, camera, options);
		FillFrameData(scene, camera, options);
		PrepareGpuResources(commandList);

		commandList->writeBuffer(m_FrameBuffer, &m_Frame, sizeof(FrameData));
		if(m_LocalLights.empty())
		{
			LocalLightGPU none{};
			commandList->writeBuffer(m_LightBuffer, &none, sizeof(none));
		}
		else
		{
			commandList->writeBuffer(m_LightBuffer, m_LocalLights.data(), m_LocalLights.size() * sizeof(LocalLightGPU));
		}

		if(m_CascadeCount > 0)
			RenderSunShadows(commandList, m_CascadeCount);
		RenderLocalShadows(commandList);

		RenderPrepass(commandList);
		if(m_Frame.SSAOParams.w > 0.5f)
			RenderAmbientOcclusion(commandList);
		RenderForward(commandList, scene);
		RenderTonemap(commandList, scene.GetSettings().PostProcess.TonemapMode);
		RenderOverlays(commandList, options, camera);
	}

}
