#include "Starfall/Core/Application.h"

#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Platform/Input.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Renderer/Texture.h"

#include <imgui.h>

#include <chrono>
#include <thread>

namespace Starfall {

	Application* Application::s_Instance = nullptr;

	Application::Application(ApplicationDesc desc)
		: m_Desc(std::move(desc))
	{
		SF_CORE_ASSERT(!s_Instance, "Only one Application may exist");
		s_Instance = this;
	}

	Application::~Application()
	{
		s_Instance = nullptr;
	}

	int Application::Run()
	{
		WindowDesc wd;
		wd.Title = m_Desc.Name;
		wd.Width = m_Desc.Width;
		wd.Height = m_Desc.Height;
		wd.Maximized = m_Desc.Maximized;
		wd.Fullscreen = m_Desc.Fullscreen;
		wd.Visible = !m_Desc.HiddenWindow;
		m_Window = Window::Create(wd);
		if(!m_Window)
			return 1;
		m_Window->OnDropFile = [this](const std::string& path) { OnFileDrop(path); };

		GraphicsDeviceDesc gd;
		gd.Window = m_Window->GetNativeWindow();
		gd.VSync = m_Desc.VSync;
		gd.EnableValidation = m_Desc.EnableValidation;
		m_Device = GraphicsDevice::Create(gd);
		if(!m_Device)
			return 2;
		m_Context = RenderContext::Create(*m_Device);
		if(!m_Context)
			return 3;

		AudioEngine::Init(false);

		if(m_Desc.UseImGui)
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
			io.IniFilename = "StarfallEditor.ini";
			m_ImGui = CreateScope<ImGuiRenderer>(*m_Context, m_Window->GetNativeWindow());
		}

		if(!OnInit())
		{
			SF_CORE_ERROR("Application initialization failed");
			m_Running = false;
		}
		else
		{
			m_Running = true;
		}

		nvrhi::CommandListHandle commandList = m_Device->CreateCommandList();
		auto last = std::chrono::steady_clock::now();
		uint32_t framesRendered = 0;

		while(m_Running && !m_Window->ShouldClose())
		{
			Input::BeginFrame();
			m_Window->PollEvents();

			auto now = std::chrono::steady_clock::now();
			m_DeltaTime = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
			last = now;
			m_Time += m_DeltaTime;

			if(m_Window->IsMinimized())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				continue;
			}

			OnUpdate(m_DeltaTime);

			if(!m_Device->BeginFrame())
				continue;

			if(m_ImGui)
			{
				m_ImGui->BeginFrame();
				ImGui::NewFrame();
				OnImGui();
				ImGui::Render();
			}

			commandList->open();
			OnRender(commandList, m_Device->GetCurrentFramebuffer());
			if(m_ImGui)
				m_ImGui->Render(commandList, m_Device->GetCurrentFramebuffer());
			commandList->close();
			m_Device->GetDevice()->executeCommandList(commandList);

			bool wantsShot = !m_PendingScreenshot.empty();
			framesRendered++;
			bool lastFrame = m_Desc.ExitAfterFrames > 0 && framesRendered >= m_Desc.ExitAfterFrames;
			if(!wantsShot && lastFrame && !m_Desc.ScreenshotPath.empty())
			{
				m_PendingScreenshot = m_Desc.ScreenshotPath;
				wantsShot = true;
			}
			if(wantsShot)
			{
				SaveScreenshot(m_Device->GetCurrentBackBuffer(), m_PendingScreenshot);
				m_PendingScreenshot.clear();
			}

			m_Device->Present();
			m_FrameIndex++;
			if(lastFrame)
				m_Running = false;
		}

		m_Device->WaitIdle();
		OnShutdown();
		m_ImGui.reset();
		if(m_Desc.UseImGui)
			ImGui::DestroyContext();
		AudioEngine::Shutdown();
		m_Context.reset();
		m_Device.reset();
		m_Window.reset();
		return 0;
	}

	void Application::SaveScreenshot(nvrhi::ITexture* backBuffer, const std::filesystem::path& path)
	{
		if(!backBuffer)
			return;
		const nvrhi::TextureDesc& td = backBuffer->getDesc();
		nvrhi::TextureDesc sd = td;
		sd.isRenderTarget = false;
		sd.initialState = nvrhi::ResourceStates::CopyDest;
		sd.keepInitialState = true;
		nvrhi::StagingTextureHandle staging = m_Device->GetDevice()->createStagingTexture(sd, nvrhi::CpuAccessMode::Read);

		nvrhi::CommandListHandle cmd = m_Device->CreateCommandList();
		cmd->open();
		cmd->copyTexture(staging, nvrhi::TextureSlice(), backBuffer, nvrhi::TextureSlice());
		cmd->close();
		m_Device->GetDevice()->executeCommandList(cmd);
		m_Device->GetDevice()->waitForIdle();

		size_t pitch = 0;
		const uint8_t* mapped = static_cast<const uint8_t*>(m_Device->GetDevice()->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
		std::vector<uint8_t> rgba(static_cast<size_t>(td.width) * td.height * 4);
		for(uint32_t y = 0; y < td.height; y++)
		{
			const uint8_t* src = mapped + y * pitch;
			uint8_t* dst = &rgba[static_cast<size_t>(y) * td.width * 4];
			for(uint32_t x = 0; x < td.width; x++)
			{
				dst[x * 4 + 0] = src[x * 4 + 2]; // BGRA -> RGBA
				dst[x * 4 + 1] = src[x * 4 + 1];
				dst[x * 4 + 2] = src[x * 4 + 0];
				dst[x * 4 + 3] = 255;
			}
		}
		m_Device->GetDevice()->unmapStagingTexture(staging);
		if(ImageIO::SavePNG(path, td.width, td.height, rgba.data()))
			SF_CORE_INFO("Screenshot saved to {0}", path.string());
		else
			SF_CORE_ERROR("Failed to save screenshot to {0}", path.string());
	}

}
