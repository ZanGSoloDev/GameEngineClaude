#include "Starfall/Renderer/ImGuiRenderer.h"

#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cstring>

namespace Starfall {

	ImGuiRenderer::ImGuiRenderer(RenderContext& context, GLFWwindow* window)
		: m_Context(context)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "starfall_nvrhi";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

		if(window)
		{
			ImGui_ImplGlfw_InitForOther(window, true);
			m_PlatformInitialized = true;
		}

		nvrhi::VertexAttributeDesc attributes[3];
		attributes[0].setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert));
		attributes[1].setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert));
		attributes[2].setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert));
		m_InputLayout = context.GetDevice()->createInputLayout(attributes, 3, context.GetShader("imgui.vert"));
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		m_Context.GetGraphicsDevice().WaitIdle();
		ImGuiIO& io = ImGui::GetIO();
		// Let ImGui forget the textures we are about to destroy.
		for(ImTextureData* tex : ImGui::GetPlatformIO().Textures)
		{
			tex->SetTexID(ImTextureID_Invalid);
			tex->SetStatus(ImTextureStatus_Destroyed);
		}
		m_Entries.clear();
		if(m_PlatformInitialized)
			ImGui_ImplGlfw_Shutdown();
		io.BackendRendererName = nullptr;
	}

	void ImGuiRenderer::BeginFrame()
	{
		m_Frame++;
		if(m_PlatformInitialized)
			ImGui_ImplGlfw_NewFrame();

		// Drop cached bindings of external textures that were not drawn recently.
		for(auto it = m_ExternalIds.begin(); it != m_ExternalIds.end();)
		{
			auto entry = m_Entries.find(it->second);
			if(entry != m_Entries.end() && m_Frame - entry->second->LastUsedFrame > 2)
			{
				m_Entries.erase(entry);
				it = m_ExternalIds.erase(it);
			}
			else
				++it;
		}
	}

	ImTextureID ImGuiRenderer::GetTextureID(nvrhi::ITexture* texture)
	{
		if(!texture)
			return ImTextureID_Invalid;
		if(auto it = m_ExternalIds.find(texture); it != m_ExternalIds.end())
		{
			m_Entries[it->second]->LastUsedFrame = m_Frame;
			return it->second;
		}
		auto entry = CreateScope<TextureEntry>();
		entry->Texture = texture;
		entry->LastUsedFrame = m_Frame;
		nvrhi::BindingSetDesc set;
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture));
		set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Context.GetLinearClampSampler()));
		set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 4));
		entry->Bindings = m_Context.GetDevice()->createBindingSet(set, m_Context.GetImGuiLayout());
		ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(entry.get()));
		m_Entries[id] = std::move(entry);
		m_ExternalIds[texture] = id;
		return id;
	}

	ImGuiRenderer::TextureEntry* ImGuiRenderer::Lookup(ImTextureID id)
	{
		auto it = m_Entries.find(id);
		return it == m_Entries.end() ? nullptr : it->second.get();
	}

	void ImGuiRenderer::UpdateTexture(nvrhi::ICommandList* commandList, ImTextureData* data)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();
		if(data->Status == ImTextureStatus_WantCreate)
		{
			auto entry = CreateScope<TextureEntry>();
			entry->ManagedByImGui = true;
			nvrhi::TextureDesc desc;
			desc.width = static_cast<uint32_t>(data->Width);
			desc.height = static_cast<uint32_t>(data->Height);
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			desc.debugName = "ImGui texture";
			entry->Texture = device->createTexture(desc);
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, entry->Texture));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Context.GetLinearClampSampler()));
			set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 4));
			entry->Bindings = device->createBindingSet(set, m_Context.GetImGuiLayout());
			commandList->writeTexture(entry->Texture, 0, 0, data->GetPixels(), static_cast<size_t>(data->GetPitch()));
			ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(entry.get()));
			m_Entries[id] = std::move(entry);
			data->SetTexID(id);
			data->SetStatus(ImTextureStatus_OK);
		}
		else if(data->Status == ImTextureStatus_WantUpdates)
		{
			TextureEntry* entry = Lookup(data->GetTexID());
			if(entry)
			{
				// Simplest correct approach: re-upload the whole atlas (updates are infrequent: glyph additions).
				commandList->writeTexture(entry->Texture, 0, 0, data->GetPixels(), static_cast<size_t>(data->GetPitch()));
			}
			data->SetStatus(ImTextureStatus_OK);
		}
		else if(data->Status == ImTextureStatus_WantDestroy && data->UnusedFrames > 2)
		{
			m_Entries.erase(data->GetTexID());
			data->SetTexID(ImTextureID_Invalid);
			data->SetStatus(ImTextureStatus_Destroyed);
		}
	}

	void ImGuiRenderer::CreatePipeline(nvrhi::IFramebuffer* framebuffer)
	{
		nvrhi::RenderState state;
		state.rasterState.setCullNone().setScissorEnable(true);
		state.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		state.blendState.targets[0].setBlendEnable(true)
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);

		nvrhi::GraphicsPipelineDesc desc;
		desc.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(m_InputLayout).setRenderState(state);
		desc.setVertexShader(m_Context.GetShader("imgui.vert")).setPixelShader(m_Context.GetShader("imgui.frag"));
		desc.addBindingLayout(m_Context.GetImGuiLayout());
		m_Pipeline = m_Context.GetDevice()->createGraphicsPipeline(desc, framebuffer);
	}

	void ImGuiRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer)
	{
		ImDrawData* drawData = ImGui::GetDrawData();
		if(!drawData || !framebuffer)
			return;

		if(drawData->Textures)
			for(ImTextureData* tex : *drawData->Textures)
				if(tex->Status != ImTextureStatus_OK)
					UpdateTexture(commandList, tex);

		int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if(fbWidth <= 0 || fbHeight <= 0 || drawData->TotalVtxCount == 0)
			return;
		if(!m_Pipeline)
			CreatePipeline(framebuffer);
		if(!m_Pipeline)
			return;

		nvrhi::IDevice* device = m_Context.GetDevice();
		size_t vertexCount = static_cast<size_t>(drawData->TotalVtxCount);
		size_t indexCount = static_cast<size_t>(drawData->TotalIdxCount);
		if(!m_VertexBuffer || m_VertexCapacity < vertexCount)
		{
			m_VertexCapacity = vertexCount + 5000;
			nvrhi::BufferDesc desc;
			desc.byteSize = m_VertexCapacity * sizeof(ImDrawVert);
			desc.isVertexBuffer = true;
			desc.initialState = nvrhi::ResourceStates::VertexBuffer;
			desc.keepInitialState = true;
			desc.debugName = "ImGui vertices";
			m_VertexBuffer = device->createBuffer(desc);
		}
		if(!m_IndexBuffer || m_IndexCapacity < indexCount)
		{
			m_IndexCapacity = indexCount + 10000;
			nvrhi::BufferDesc desc;
			desc.byteSize = m_IndexCapacity * sizeof(ImDrawIdx);
			desc.isIndexBuffer = true;
			desc.initialState = nvrhi::ResourceStates::IndexBuffer;
			desc.keepInitialState = true;
			desc.debugName = "ImGui indices";
			m_IndexBuffer = device->createBuffer(desc);
		}

		std::vector<ImDrawVert> vertices;
		std::vector<ImDrawIdx> indices;
		vertices.reserve(vertexCount);
		indices.reserve(indexCount);
		for(const ImDrawList* list : drawData->CmdLists)
		{
			vertices.insert(vertices.end(), list->VtxBuffer.Data, list->VtxBuffer.Data + list->VtxBuffer.Size);
			indices.insert(indices.end(), list->IdxBuffer.Data, list->IdxBuffer.Data + list->IdxBuffer.Size);
		}
		commandList->writeBuffer(m_VertexBuffer, vertices.data(), vertices.size() * sizeof(ImDrawVert));
		commandList->writeBuffer(m_IndexBuffer, indices.data(), indices.size() * sizeof(ImDrawIdx));

		float L = drawData->DisplayPos.x, R = drawData->DisplayPos.x + drawData->DisplaySize.x;
		float T = drawData->DisplayPos.y, B = drawData->DisplayPos.y + drawData->DisplaySize.y;
		float push[4] = { 2.0f / (R - L), 2.0f / (T - B), (R + L) / (L - R), (T + B) / (B - T) };
		float scale[4] = { push[0], push[1], push[2], push[3] };

		ImVec2 clipOff = drawData->DisplayPos;
		ImVec2 clipScale = drawData->FramebufferScale;

		uint32_t vertexOffset = 0, indexOffset = 0;
		for(const ImDrawList* list : drawData->CmdLists)
		{
			for(const ImDrawCmd& cmd : list->CmdBuffer)
			{
				if(cmd.UserCallback)
				{
					if(cmd.UserCallback != ImDrawCallback_ResetRenderState)
						cmd.UserCallback(list, &cmd);
					continue;
				}
				ImVec2 clipMin((cmd.ClipRect.x - clipOff.x) * clipScale.x, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((cmd.ClipRect.z - clipOff.x) * clipScale.x, (cmd.ClipRect.w - clipOff.y) * clipScale.y);
				clipMin.x = std::max(clipMin.x, 0.0f);
				clipMin.y = std::max(clipMin.y, 0.0f);
				clipMax.x = std::min(clipMax.x, static_cast<float>(fbWidth));
				clipMax.y = std::min(clipMax.y, static_cast<float>(fbHeight));
				if(clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
					continue;

				TextureEntry* entry = Lookup(cmd.GetTexID());
				if(!entry)
					continue;

				nvrhi::GraphicsState state;
				state.setPipeline(m_Pipeline).setFramebuffer(framebuffer);
				state.setViewport(nvrhi::ViewportState().addViewport(nvrhi::Viewport(static_cast<float>(fbWidth), static_cast<float>(fbHeight)))
					.addScissorRect(nvrhi::Rect(static_cast<int>(clipMin.x), static_cast<int>(clipMax.x), static_cast<int>(clipMin.y), static_cast<int>(clipMax.y))));
				state.addBindingSet(entry->Bindings);
				state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(m_VertexBuffer).setSlot(0));
				state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(m_IndexBuffer).setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT));
				commandList->setGraphicsState(state);
				commandList->setPushConstants(scale, sizeof(scale));
				commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(cmd.ElemCount)
					.setStartIndexLocation(indexOffset + cmd.IdxOffset).setStartVertexLocation(vertexOffset + cmd.VtxOffset));
			}
			vertexOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
			indexOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
		}
	}

}
