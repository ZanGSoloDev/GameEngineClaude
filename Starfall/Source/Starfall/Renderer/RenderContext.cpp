#include "Starfall/Renderer/RenderContext.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Renderer/AssetManager.h"

namespace Starfall {

	nvrhi::VulkanBindingOffsets ZeroBindingOffsets()
	{
		// Shaders are authored in GLSL with explicit binding numbers, so no HLSL register shifting is applied.
		return nvrhi::VulkanBindingOffsets().setShaderResourceOffset(0).setSamplerOffset(0).setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
	}

	namespace {

		nvrhi::ShaderType StageFromName(const std::string& name)
		{
			if(name.ends_with(".vert")) return nvrhi::ShaderType::Vertex;
			if(name.ends_with(".frag")) return nvrhi::ShaderType::Pixel;
			if(name.ends_with(".comp")) return nvrhi::ShaderType::Compute;
			return nvrhi::ShaderType::None;
		}

	}

	Scope<RenderContext> RenderContext::Create(GraphicsDevice& graphicsDevice)
	{
		Scope<RenderContext> context(new RenderContext(graphicsDevice));
		if(!context->Init())
			return nullptr;
		return context;
	}

	RenderContext::~RenderContext()
	{
		m_GraphicsDevice.WaitIdle();
		AssetManager::ReleaseGPU();
	}

	bool RenderContext::Init()
	{
		nvrhi::IDevice* device = GetDevice();

		// Fail early with a clear message when the shaders were not built/deployed next to the executable.
		if(!std::filesystem::exists(FileSystem::GetResourcesDirectory() / "Shaders" / "pbr.frag.spv"))
		{
			SF_CORE_ERROR("Shaders not found in '{0}'. Build the project or copy the Resources directory next to the executable.",
				(FileSystem::GetResourcesDirectory() / "Shaders").string());
			return false;
		}

		m_LinearClamp = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		m_PointClamp = device->createSampler(nvrhi::SamplerDesc().setAllFilters(false).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		m_ShadowCompare = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setMipFilter(false).setAllAddressModes(nvrhi::SamplerAddressMode::Border)
			.setBorderColor(nvrhi::Color(1.0f)).setReductionType(nvrhi::SamplerReductionType::Comparison));
		m_MaterialSampler = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap).setMaxAnisotropy(8.0f));
		m_EquirectSampler = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAddressU(nvrhi::SamplerAddressMode::Wrap).setAddressV(nvrhi::SamplerAddressMode::Clamp)
			.setAddressW(nvrhi::SamplerAddressMode::Clamp));

		nvrhi::BindingLayoutDesc global;
		global.setVisibility(nvrhi::ShaderType::All).setBindingOffsets(ZeroBindingOffsets()).setRegisterSpaceAndDescriptorSet(0);
		global.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		global.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		global.addItem(nvrhi::BindingLayoutItem::Sampler(2));
		global.addItem(nvrhi::BindingLayoutItem::Sampler(3));
		global.addItem(nvrhi::BindingLayoutItem::PushConstants(4, sizeof(MeshPushConstants)));
		m_GlobalLayout = device->createBindingLayout(global);

		nvrhi::BindingLayoutDesc material;
		material.setVisibility(nvrhi::ShaderType::Pixel).setBindingOffsets(ZeroBindingOffsets()).setRegisterSpaceAndDescriptorSet(2);
		material.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
		for(uint32_t i = 1; i <= 5; i++)
			material.addItem(nvrhi::BindingLayoutItem::Texture_SRV(i));
		material.addItem(nvrhi::BindingLayoutItem::Sampler(6));
		m_MaterialLayout = device->createBindingLayout(material);

		nvrhi::BindingLayoutDesc imgui;
		imgui.setVisibility(nvrhi::ShaderType::All).setBindingOffsets(ZeroBindingOffsets()).setRegisterSpaceAndDescriptorSet(0);
		imgui.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		imgui.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		imgui.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float) * 4));
		m_ImGuiLayout = device->createBindingLayout(imgui);

		nvrhi::TextureDesc lut;
		lut.width = lut.height = BrdfLutSize;
		lut.format = nvrhi::Format::RG16_FLOAT;
		lut.isUAV = true;
		lut.initialState = nvrhi::ResourceStates::ShaderResource;
		lut.keepInitialState = true;
		lut.debugName = "BRDF LUT";
		m_BrdfLut = device->createTexture(lut);
		return true;
	}

	nvrhi::IShader* RenderContext::GetShader(const std::string& name)
	{
		if(auto it = m_Shaders.find(name); it != m_Shaders.end())
			return it->second;

		nvrhi::ShaderHandle shader;
		auto bytes = FileSystem::ReadBinary(FileSystem::GetResourcesDirectory() / "Shaders" / (name + ".spv"));
		nvrhi::ShaderType type = StageFromName(name);
		if(!bytes || bytes->empty() || type == nvrhi::ShaderType::None)
		{
			SF_CORE_ERROR("Shader '{0}' not found", name);
		}
		else
		{
			nvrhi::ShaderDesc desc(type);
			desc.setDebugName(name);
			desc.setEntryName("main");
			shader = GetDevice()->createShader(desc, bytes->data(), bytes->size());
		}
		m_Shaders[name] = shader;
		return shader;
	}

	nvrhi::BufferHandle RenderContext::CreateVolatileConstantBuffer(size_t byteSize, const char* name) const
	{
		nvrhi::BufferDesc desc;
		desc.byteSize = (byteSize + 15) & ~size_t(15);
		desc.isConstantBuffer = true;
		desc.isVolatile = true;
		desc.maxVersions = 16;
		desc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		desc.keepInitialState = true;
		desc.debugName = name;
		return GetDevice()->createBuffer(desc);
	}

	nvrhi::ComputePipelineHandle RenderContext::CreateComputePipeline(const std::string& shader, nvrhi::IBindingLayout* layout)
	{
		nvrhi::IShader* cs = GetShader(shader);
		if(!cs)
			return nullptr;
		nvrhi::ComputePipelineDesc desc;
		desc.setComputeShader(cs);
		desc.addBindingLayout(layout);
		return GetDevice()->createComputePipeline(desc);
	}

	void RenderContext::PrepareMesh(nvrhi::ICommandList* commandList, Mesh& mesh)
	{
		mesh.EnsureGPU(GetDevice(), commandList);
	}

	nvrhi::IBindingSet* RenderContext::PrepareMaterial(nvrhi::ICommandList* commandList, Material& material)
	{
		if(material.BindingSet && !material.IsDirty())
			return material.BindingSet;

		nvrhi::IDevice* device = GetDevice();
		const MaterialData& d = material.GetData();

		MaterialGPU gpu;
		gpu.BaseColor = d.BaseColor;
		gpu.EmissiveIntensity = glm::vec4(d.Emissive, d.EmissiveIntensity);
		gpu.Params0 = glm::vec4(d.Metallic, d.Roughness, d.NormalScale, d.OcclusionStrength);
		gpu.Params1 = glm::vec4(d.AlphaCutoff, static_cast<float>(d.Alpha), d.DoubleSided ? 1.0f : 0.0f, 0.0f);
		gpu.UVTransform = glm::vec4(d.UVScale, d.UVOffset);

		if(!material.ConstantBuffer)
		{
			nvrhi::BufferDesc desc;
			desc.byteSize = sizeof(MaterialGPU);
			desc.isConstantBuffer = true;
			desc.initialState = nvrhi::ResourceStates::ConstantBuffer;
			desc.keepInitialState = true;
			desc.debugName = material.GetName();
			material.ConstantBuffer = device->createBuffer(desc);
		}
		commandList->writeBuffer(material.ConstantBuffer, &gpu, sizeof(gpu));

		nvrhi::BindingSetDesc set;
		set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, material.ConstantBuffer));
		const TextureSlot slots[] = { TextureSlot::BaseColor, TextureSlot::MetallicRoughness, TextureSlot::Normal, TextureSlot::Occlusion, TextureSlot::Emissive };
		for(uint32_t i = 0; i < 5; i++)
		{
			Ref<Texture2D> texture = material.GetTexture(slots[i]);
			if(!texture)
				texture = slots[i] == TextureSlot::Normal ? AssetManager::GetFlatNormalTexture() : AssetManager::GetWhiteTexture();
			texture->EnsureGPU(device, commandList);
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(i + 1, texture->GetTexture()));
		}
		set.addItem(nvrhi::BindingSetItem::Sampler(6, m_MaterialSampler));
		material.BindingSet = device->createBindingSet(set, m_MaterialLayout);
		material.ClearDirty();
		return material.BindingSet;
	}

	void RenderContext::EnsureBrdfLut(nvrhi::ICommandList* commandList)
	{
		if(m_BrdfLutBuilt)
			return;
		m_BrdfLutBuilt = true;

		nvrhi::IDevice* device = GetDevice();
		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.setVisibility(nvrhi::ShaderType::Compute).setBindingOffsets(ZeroBindingOffsets());
		layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2));
		layoutDesc.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::vec4)));
		nvrhi::BindingLayoutHandle layout = device->createBindingLayout(layoutDesc);

		nvrhi::BindingSetDesc setDesc;
		setDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(2, m_BrdfLut));
		setDesc.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::vec4)));
		nvrhi::BindingSetHandle set = device->createBindingSet(setDesc, layout);

		nvrhi::ComputePipelineHandle pipeline = CreateComputePipeline("brdf_lut.comp", layout);
		if(!pipeline)
			return;
		nvrhi::ComputeState state;
		state.setPipeline(pipeline);
		state.addBindingSet(set);
		commandList->setComputeState(state);
		glm::vec4 params(static_cast<float>(BrdfLutSize), 0, 0, 0);
		commandList->setPushConstants(&params, sizeof(params));
		commandList->dispatch(BrdfLutSize / 8, BrdfLutSize / 8, 1);
	}

}
