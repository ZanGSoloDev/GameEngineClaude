#include "Starfall/Platform/Window.h"

#include "Starfall/Platform/Input.h"

#include <GLFW/glfw3.h>

namespace Starfall {

	namespace {

		int s_WindowCount = 0;
		bool s_GLFWInitialized = false;

		void GLFWErrorCallback(int error, const char* description)
		{
			SF_CORE_ERROR("GLFW error {0}: {1}", error, description);
		}

		Window* From(GLFWwindow* window) { return static_cast<Window*>(glfwGetWindowUserPointer(window)); }

		bool InitGLFW()
		{
			if(s_GLFWInitialized)
				return true;
			glfwSetErrorCallback(GLFWErrorCallback);
			if(!glfwInit())
			{
				SF_CORE_ERROR("Failed to initialize GLFW");
				return false;
			}
			s_GLFWInitialized = true;
			return true;
		}

	}

	Scope<Window> Window::Create(const WindowDesc& desc)
	{
		if(!InitGLFW())
			return nullptr;

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan: no OpenGL context
		glfwWindowHint(GLFW_VISIBLE, desc.Visible ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, desc.Resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, desc.Maximized ? GLFW_TRUE : GLFW_FALSE);

		GLFWmonitor* monitor = nullptr;
		int width = static_cast<int>(desc.Width);
		int height = static_cast<int>(desc.Height);
		if(desc.Fullscreen)
		{
			monitor = glfwGetPrimaryMonitor();
			if(const GLFWvidmode* mode = glfwGetVideoMode(monitor))
			{
				width = mode->width;
				height = mode->height;
			}
		}

		GLFWwindow* handle = glfwCreateWindow(width, height, desc.Title.c_str(), monitor, nullptr);
		if(!handle)
		{
			SF_CORE_ERROR("Failed to create window");
			return nullptr;
		}

		Scope<Window> window(new Window());
		window->m_Window = handle;
		window->m_Fullscreen = desc.Fullscreen;
		s_WindowCount++;
		glfwSetWindowUserPointer(handle, window.get());

		glfwSetFramebufferSizeCallback(handle, [](GLFWwindow* w, int width, int height) {
			if(Window* self = From(w); self && self->OnResize)
				self->OnResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		});
		glfwSetKeyCallback(handle, [](GLFWwindow*, int key, int, int action, int) {
			if(action == GLFW_REPEAT || key < 0)
				return;
			Input::OnKey(static_cast<KeyCode>(key), action == GLFW_PRESS);
		});
		glfwSetMouseButtonCallback(handle, [](GLFWwindow*, int button, int action, int) {
			if(button >= 0 && button <= 2)
				Input::OnMouseButton(static_cast<MouseButton>(button), action == GLFW_PRESS);
		});
		glfwSetCursorPosCallback(handle, [](GLFWwindow*, double x, double y) { Input::OnMouseMoved(static_cast<float>(x), static_cast<float>(y)); });
		glfwSetScrollCallback(handle, [](GLFWwindow*, double dx, double dy) { Input::OnScroll(static_cast<float>(dx), static_cast<float>(dy)); });
		glfwSetDropCallback(handle, [](GLFWwindow* w, int count, const char** paths) {
			if(Window* self = From(w); self && self->OnDropFile)
				for(int i = 0; i < count; i++)
					self->OnDropFile(paths[i]);
		});
		return window;
	}

	Window::~Window()
	{
		if(m_Window)
			glfwDestroyWindow(m_Window);
		if(--s_WindowCount == 0 && s_GLFWInitialized)
		{
			glfwTerminate();
			s_GLFWInitialized = false;
		}
	}

	void Window::PollEvents() { glfwPollEvents(); }
	bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Window) != 0; }
	void Window::RequestClose() { glfwSetWindowShouldClose(m_Window, GLFW_TRUE); }

	uint32_t Window::GetWidth() const
	{
		int w = 0, h = 0;
		glfwGetWindowSize(m_Window, &w, &h);
		return static_cast<uint32_t>(w);
	}

	uint32_t Window::GetHeight() const
	{
		int w = 0, h = 0;
		glfwGetWindowSize(m_Window, &w, &h);
		return static_cast<uint32_t>(h);
	}

	void Window::GetFramebufferSize(uint32_t& width, uint32_t& height) const
	{
		int w = 0, h = 0;
		glfwGetFramebufferSize(m_Window, &w, &h);
		width = static_cast<uint32_t>(w);
		height = static_cast<uint32_t>(h);
	}

	bool Window::IsMinimized() const
	{
		uint32_t w, h;
		GetFramebufferSize(w, h);
		return w == 0 || h == 0 || glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED);
	}

	bool Window::IsFocused() const { return glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0; }
	void Window::SetTitle(const std::string& title) { glfwSetWindowTitle(m_Window, title.c_str()); }

	void Window::SetCursorMode(CursorMode mode)
	{
		int value = mode == CursorMode::Normal ? GLFW_CURSOR_NORMAL : mode == CursorMode::Hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED;
		glfwSetInputMode(m_Window, GLFW_CURSOR, value);
	}

	void Window::SetFullscreen(bool fullscreen)
	{
		if(fullscreen == m_Fullscreen)
			return;
		if(fullscreen)
		{
			glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
			glfwGetWindowSize(m_Window, &m_WindowedW, &m_WindowedH);
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			if(const GLFWvidmode* mode = glfwGetVideoMode(monitor))
				glfwSetWindowMonitor(m_Window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
		}
		else
		{
			glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, m_WindowedW, m_WindowedH, 0);
		}
		m_Fullscreen = fullscreen;
	}

}
