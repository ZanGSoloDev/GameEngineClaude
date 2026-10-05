#pragma once

#include "Starfall/Core/Base.h"

#include <nvrhi/nvrhi.h>

namespace nvrhi::vulkan { class IDevice; }

#include <string>
#include <vector>

struct GLFWwindow;

namespace Starfall {

	struct GraphicsDeviceDesc
	{
		bool EnableValidation = false; // Vulkan validation layer (if installed) + nvrhi validation layer
		GLFWwindow* Window = nullptr;  // null = headless (offscreen only, no swapchain)
		bool VSync = true;
	};

	// Owns the Vulkan instance/device, an optional swapchain and the nvrhi device used by every renderer system.
	class GraphicsDevice
	{
	public:
		~GraphicsDevice();
		GraphicsDevice(const GraphicsDevice&) = delete;
		GraphicsDevice& operator=(const GraphicsDevice&) = delete;

		// Returns null (after logging the reason) when Vulkan is unavailable.
		static Scope<GraphicsDevice> Create(const GraphicsDeviceDesc& desc);

		nvrhi::IDevice* GetDevice() const { return m_NvrhiDevice; }
		const std::string& GetAdapterName() const { return m_AdapterName; }

		// Swapchain (window mode only)
		bool HasSwapchain() const { return m_Swapchain != nullptr; }
		void Resize(uint32_t width, uint32_t height);
		void SetVSync(bool vsync);
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		nvrhi::Format GetSwapchainFormat() const { return nvrhi::Format::BGRA8_UNORM; }

		// Frame: BeginFrame acquires the back buffer (returns false if the swapchain is out of date / minimized),
		// Present submits and presents it. GetCurrentFramebuffer() is only valid between the two.
		bool BeginFrame();
		void Present();
		nvrhi::IFramebuffer* GetCurrentFramebuffer() const;
		nvrhi::ITexture* GetCurrentBackBuffer() const;

		void WaitIdle();

		// Headless helpers
		nvrhi::CommandListHandle CreateCommandList();
		void ExecuteAndWait(nvrhi::ICommandList* commandList);

	private:
		GraphicsDevice() = default;

		bool Init(const GraphicsDeviceDesc& desc);
		nvrhi::vulkan::IDevice* VulkanDevice() const;
		bool CreateSwapchain();
		void DestroySwapchain();

		struct Vulkan;
		Scope<Vulkan> m_Vk;
		nvrhi::DeviceHandle m_NvrhiDevice;
		std::string m_AdapterName;
		bool m_VSync = true;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_SwapchainDirty = false;

		struct SwapchainState;
		Scope<SwapchainState> m_Swapchain;
	};

}
