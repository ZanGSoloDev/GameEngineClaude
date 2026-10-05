#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Renderer/RenderContext.h"

#include <imgui.h>

#include <unordered_map>

struct GLFWwindow;

namespace Starfall {

	// Dear ImGui platform (GLFW) + renderer (nvrhi) backend.
	class ImGuiRenderer
	{
	public:
		// window may be null for headless use (renderer only, no platform backend).
		ImGuiRenderer(RenderContext& context, GLFWwindow* window);
		~ImGuiRenderer();
		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

		void BeginFrame();
		// Renders ImGui::GetDrawData() into the framebuffer using an open command list.
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer);

		// Makes an engine texture usable with ImGui::Image. Valid for the current frame; call every frame you draw it.
		ImTextureID GetTextureID(nvrhi::ITexture* texture);

	private:
		struct TextureEntry
		{
			nvrhi::TextureHandle Texture;
			nvrhi::BindingSetHandle Bindings;
			bool ManagedByImGui = false;
			uint64_t LastUsedFrame = 0;
		};

		void UpdateTexture(nvrhi::ICommandList* commandList, ImTextureData* data);
		TextureEntry* Lookup(ImTextureID id);
		void CreatePipeline(nvrhi::IFramebuffer* framebuffer);

		RenderContext& m_Context;
		bool m_PlatformInitialized = false;
		uint64_t m_Frame = 0;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BufferHandle m_VertexBuffer, m_IndexBuffer;
		size_t m_VertexCapacity = 0, m_IndexCapacity = 0;
		std::unordered_map<ImTextureID, Scope<TextureEntry>> m_Entries;
		std::unordered_map<nvrhi::ITexture*, ImTextureID> m_ExternalIds;
	};

}
