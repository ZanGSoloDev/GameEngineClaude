#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Core/Timestep.h"
#include "Starfall/Platform/Window.h"
#include "Starfall/Renderer/GraphicsDevice.h"
#include "Starfall/Renderer/ImGuiRenderer.h"
#include "Starfall/Renderer/RenderContext.h"

#include <filesystem>
#include <string>

namespace Starfall {

	struct ApplicationDesc
	{
		std::string Name = "Starfall";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Maximized = false;
		bool Fullscreen = false;
		bool VSync = true;
		bool EnableValidation = false;
		bool UseImGui = false;

		// Automation (tests, CI, documentation screenshots)
		std::filesystem::path ScreenshotPath;  // saved after `ExitAfterFrames` frames
		uint32_t ExitAfterFrames = 0;          // 0 = run until closed
		bool HiddenWindow = false;
	};

	// Owns the window, graphics device, render context and the main loop. Derive and override the On* hooks.
	class Application
	{
	public:
		explicit Application(ApplicationDesc desc);
		virtual ~Application();
		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		// Creates the window/device and runs until closed. Returns the process exit code.
		int Run();
		void Close() { m_Running = false; }

		static Application& Get() { return *s_Instance; }
		Window& GetWindow() { return *m_Window; }
		GraphicsDevice& GetGraphicsDevice() { return *m_Device; }
		RenderContext& GetRenderContext() { return *m_Context; }
		ImGuiRenderer* GetImGui() { return m_ImGui.get(); }
		const ApplicationDesc& GetDesc() const { return m_Desc; }
		uint64_t GetFrameIndex() const { return m_FrameIndex; }
		float GetTime() const { return m_Time; }
		float GetDeltaTime() const { return m_DeltaTime; }

		// Saves the back buffer of the current frame to a PNG after rendering finishes.
		void RequestScreenshot(const std::filesystem::path& path) { m_PendingScreenshot = path; }

	protected:
		virtual bool OnInit() { return true; }
		virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
		// Record rendering into the open command list; the back buffer framebuffer is passed for direct drawing.
		virtual void OnRender(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* backBuffer) { (void)commandList; (void)backBuffer; }
		virtual void OnImGui() {}
		virtual void OnShutdown() {}
		virtual void OnFileDrop(const std::string& path) { (void)path; }

	private:
		void SaveScreenshot(nvrhi::ITexture* backBuffer, const std::filesystem::path& path);

		ApplicationDesc m_Desc;
		Scope<Window> m_Window;
		Scope<GraphicsDevice> m_Device;
		Scope<RenderContext> m_Context;
		Scope<ImGuiRenderer> m_ImGui;
		bool m_Running = false;
		uint64_t m_FrameIndex = 0;
		float m_Time = 0.0f;
		float m_DeltaTime = 0.0f;
		std::filesystem::path m_PendingScreenshot;
		static Application* s_Instance;
	};

}
