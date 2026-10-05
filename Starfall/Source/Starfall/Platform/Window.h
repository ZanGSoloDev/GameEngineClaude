#pragma once

#include "Starfall/Core/Base.h"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Starfall {

	struct WindowDesc
	{
		std::string Title = "Starfall";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Visible = true;       // hidden windows are used for offscreen rendering / tests
		bool Resizable = true;
		bool Maximized = false;
		bool Fullscreen = false;
	};

	enum class CursorMode { Normal, Hidden, Locked };

	// GLFW window. Forwards keyboard/mouse events to Input.
	class Window
	{
	public:
		~Window();
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		// Initializes GLFW on first use. Returns null (and logs) if the window cannot be created.
		static Scope<Window> Create(const WindowDesc& desc);

		void PollEvents();
		bool ShouldClose() const;
		void RequestClose();

		uint32_t GetWidth() const;   // window size in screen coordinates
		uint32_t GetHeight() const;
		void GetFramebufferSize(uint32_t& width, uint32_t& height) const;
		bool IsMinimized() const;
		bool IsFocused() const;

		void SetTitle(const std::string& title);
		void SetCursorMode(CursorMode mode);
		void SetFullscreen(bool fullscreen);
		bool IsFullscreen() const { return m_Fullscreen; }

		GLFWwindow* GetNativeWindow() const { return m_Window; }

		// Fired when the framebuffer size changes (also while the user drags the window edge).
		std::function<void(uint32_t, uint32_t)> OnResize;
		std::function<void(const std::string&)> OnDropFile;

	private:
		Window() = default;

		GLFWwindow* m_Window = nullptr;
		bool m_Fullscreen = false;
		int m_WindowedX = 0, m_WindowedY = 0, m_WindowedW = 0, m_WindowedH = 0;
	};

}
