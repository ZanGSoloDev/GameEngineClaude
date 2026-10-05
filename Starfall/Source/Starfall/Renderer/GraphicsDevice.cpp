#include "Starfall/Renderer/GraphicsDevice.h"

// nvrhi_vk expects the host application to provide the vulkan.hpp dispatcher storage.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <set>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

namespace Starfall {

	namespace {

		constexpr uint32_t FramesInFlight = 2;

		class NvrhiMessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			void message(nvrhi::MessageSeverity severity, const char* text) override
			{
				switch(severity)
				{
					case nvrhi::MessageSeverity::Info: SF_CORE_INFO("nvrhi: {0}", text); break;
					case nvrhi::MessageSeverity::Warning: SF_CORE_WARN("nvrhi: {0}", text); break;
					case nvrhi::MessageSeverity::Error: SF_CORE_ERROR("nvrhi: {0}", text); break;
					case nvrhi::MessageSeverity::Fatal: SF_CORE_CRITICAL("nvrhi: {0}", text); break;
				}
			}
		};

		NvrhiMessageCallback s_MessageCallback;

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
		{
			if(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				SF_CORE_ERROR("Vulkan: {0}", data->pMessage);
			else if(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
				SF_CORE_WARN("Vulkan: {0}", data->pMessage);
			return VK_FALSE;
		}

		bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
		{
			return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
		}

	}

	struct GraphicsDevice::Vulkan
	{
		void* Library = nullptr;
		PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

		// global
		PFN_vkCreateInstance CreateInstance = nullptr;
		PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
		PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayerProperties = nullptr;
		// instance
		PFN_vkDestroyInstance DestroyInstance = nullptr;
		PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
		PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
		PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
		PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
		PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
		PFN_vkCreateDevice CreateDevice = nullptr;
		PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
		PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR = nullptr;
		PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
		PFN_vkCreateDebugUtilsMessengerEXT CreateDebugUtilsMessengerEXT = nullptr;
		PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT = nullptr;
		// device
		PFN_vkDestroyDevice DestroyDevice = nullptr;
		PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
		PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
		PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
		PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
		PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
		PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
		PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
		PFN_vkCreateSemaphore CreateSemaphoreFn = nullptr;
		PFN_vkDestroySemaphore DestroySemaphoreFn = nullptr;

		VkInstance Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT Messenger = VK_NULL_HANDLE;
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		VkQueue Queue = VK_NULL_HANDLE;
		uint32_t QueueFamily = 0;
		GLFWwindow* Window = nullptr;

		std::vector<std::string> InstanceExtensions;
		std::vector<std::string> DeviceExtensions;

		bool LoadVulkanLibrary()
		{
#if defined(_WIN32)
			Library = ::LoadLibraryA("vulkan-1.dll");
			if(Library)
				GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::GetProcAddress(static_cast<HMODULE>(Library), "vkGetInstanceProcAddr"));
#else
	#if defined(__APPLE__)
			const char* names[] = { "libvulkan.1.dylib", "libMoltenVK.dylib" };
	#else
			const char* names[] = { "libvulkan.so.1", "libvulkan.so" };
	#endif
			for(const char* name : names)
			{
				Library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
				if(Library)
					break;
			}
			if(Library)
				GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(Library, "vkGetInstanceProcAddr"));
#endif
			if(!GetInstanceProcAddr)
				return false;
			CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(GetInstanceProcAddr(nullptr, "vkCreateInstance"));
			EnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(GetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
			EnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(GetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
			return CreateInstance && EnumerateInstanceExtensionProperties && EnumerateInstanceLayerProperties;
		}

		void UnloadVulkanLibrary()
		{
			if(!Library)
				return;
#if defined(_WIN32)
			::FreeLibrary(static_cast<HMODULE>(Library));
#else
			dlclose(Library);
#endif
			Library = nullptr;
		}

		template<typename T>
		T LoadInstanceFn(const char* name) const { return reinterpret_cast<T>(GetInstanceProcAddr(Instance, name)); }
		template<typename T>
		T LoadDeviceFn(const char* name) const { return reinterpret_cast<T>(GetDeviceProcAddr(Device, name)); }
	};

	struct GraphicsDevice::SwapchainState
	{
		VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> Images;
		std::vector<nvrhi::TextureHandle> Textures;
		std::vector<nvrhi::FramebufferHandle> Framebuffers;
		std::vector<VkSemaphore> AcquireSemaphores;
		std::vector<VkSemaphore> PresentSemaphores;
		nvrhi::EventQueryHandle FrameQueries[FramesInFlight];
		bool QuerySubmitted[FramesInFlight] = {};
		uint32_t ImageIndex = 0;
		uint32_t FrameIndex = 0;
		bool FrameActive = false;
	};

	GraphicsDevice::~GraphicsDevice()
	{
		if(m_Vk && m_Vk->Device && m_NvrhiDevice)
			m_NvrhiDevice->waitForIdle();
		DestroySwapchain();
		m_NvrhiDevice = nullptr;

		if(!m_Vk)
			return;
		if(m_Vk->Device)
			m_Vk->DestroyDevice(m_Vk->Device, nullptr);
		if(m_Vk->Surface)
			m_Vk->DestroySurfaceKHR(m_Vk->Instance, m_Vk->Surface, nullptr);
		if(m_Vk->Messenger && m_Vk->DestroyDebugUtilsMessengerEXT)
			m_Vk->DestroyDebugUtilsMessengerEXT(m_Vk->Instance, m_Vk->Messenger, nullptr);
		if(m_Vk->Instance)
			m_Vk->DestroyInstance(m_Vk->Instance, nullptr);
		m_Vk->UnloadVulkanLibrary();
	}

	nvrhi::vulkan::IDevice* GraphicsDevice::VulkanDevice() const
	{
		// The validation layer wraps the device, so ask for the native Vulkan one.
		return static_cast<nvrhi::vulkan::IDevice*>(m_NvrhiDevice->getNativeObject(nvrhi::ObjectTypes::Nvrhi_VK_Device).pointer);
	}

	Scope<GraphicsDevice> GraphicsDevice::Create(const GraphicsDeviceDesc& desc)
	{
		Scope<GraphicsDevice> device(new GraphicsDevice());
		if(!device->Init(desc))
			return nullptr;
		return device;
	}

	bool GraphicsDevice::Init(const GraphicsDeviceDesc& desc)
	{
		m_Vk = CreateScope<Vulkan>();
		m_Vk->Window = desc.Window;
		m_VSync = desc.VSync;
		Vulkan& vk = *m_Vk;

		if(!vk.LoadVulkanLibrary())
		{
			SF_CORE_ERROR("Vulkan loader not found. Install a Vulkan capable graphics driver.");
			return false;
		}

		// ---- Instance ----
		uint32_t extCount = 0;
		vk.EnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
		std::vector<VkExtensionProperties> availableInstanceExtensions(extCount);
		vk.EnumerateInstanceExtensionProperties(nullptr, &extCount, availableInstanceExtensions.data());

		std::set<std::string> instanceExtensions;
		if(desc.Window)
		{
			if(!glfwVulkanSupported())
			{
				SF_CORE_ERROR("GLFW reports Vulkan is not supported on this system");
				return false;
			}
			uint32_t glfwCount = 0;
			const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
			for(uint32_t i = 0; i < glfwCount; i++)
				instanceExtensions.insert(glfwExtensions[i]);
		}
		VkInstanceCreateFlags instanceFlags = 0;
		if(HasExtension(availableInstanceExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			instanceExtensions.insert(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}
		bool debugUtils = HasExtension(availableInstanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if(debugUtils)
			instanceExtensions.insert(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		for(const auto& ext : instanceExtensions)
		{
			if(!HasExtension(availableInstanceExtensions, ext.c_str()))
			{
				SF_CORE_ERROR("Required Vulkan instance extension missing: {0}", ext);
				return false;
			}
			vk.InstanceExtensions.push_back(ext);
		}

		std::vector<const char*> layers;
		if(desc.EnableValidation)
		{
			uint32_t layerCount = 0;
			vk.EnumerateInstanceLayerProperties(&layerCount, nullptr);
			std::vector<VkLayerProperties> available(layerCount);
			vk.EnumerateInstanceLayerProperties(&layerCount, available.data());
			for(const auto& layer : available)
				if(std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
					layers.push_back("VK_LAYER_KHRONOS_validation");
			if(layers.empty())
				SF_CORE_WARN("Vulkan validation requested but VK_LAYER_KHRONOS_validation is not installed");
		}

		std::vector<const char*> instanceExtensionPtrs;
		for(const auto& e : vk.InstanceExtensions)
			instanceExtensionPtrs.push_back(e.c_str());

		VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		appInfo.pApplicationName = "Starfall";
		appInfo.pEngineName = "Starfall";
		appInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.flags = instanceFlags;
		instanceInfo.pApplicationInfo = &appInfo;
		instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensionPtrs.size());
		instanceInfo.ppEnabledExtensionNames = instanceExtensionPtrs.data();
		instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
		instanceInfo.ppEnabledLayerNames = layers.data();

		VkResult result = vk.CreateInstance(&instanceInfo, nullptr, &vk.Instance);
		if(result != VK_SUCCESS)
		{
			SF_CORE_ERROR("vkCreateInstance failed ({0}); Vulkan 1.3 is required", static_cast<int>(result));
			return false;
		}

		vk.DestroyInstance = vk.LoadInstanceFn<PFN_vkDestroyInstance>("vkDestroyInstance");
		vk.EnumeratePhysicalDevices = vk.LoadInstanceFn<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
		vk.GetPhysicalDeviceProperties = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
		vk.GetPhysicalDeviceQueueFamilyProperties = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
		vk.GetPhysicalDeviceFeatures2 = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2");
		vk.EnumerateDeviceExtensionProperties = vk.LoadInstanceFn<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
		vk.CreateDevice = vk.LoadInstanceFn<PFN_vkCreateDevice>("vkCreateDevice");
		vk.GetDeviceProcAddr = vk.LoadInstanceFn<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
		vk.GetPhysicalDeviceSurfaceSupportKHR = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR");
		vk.GetPhysicalDeviceSurfaceCapabilitiesKHR = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
		vk.GetPhysicalDeviceSurfaceFormatsKHR = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR");
		vk.GetPhysicalDeviceSurfacePresentModesKHR = vk.LoadInstanceFn<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR");
		vk.DestroySurfaceKHR = vk.LoadInstanceFn<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");

		if(debugUtils && !layers.empty())
		{
			vk.CreateDebugUtilsMessengerEXT = vk.LoadInstanceFn<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT");
			vk.DestroyDebugUtilsMessengerEXT = vk.LoadInstanceFn<PFN_vkDestroyDebugUtilsMessengerEXT>("vkDestroyDebugUtilsMessengerEXT");
			if(vk.CreateDebugUtilsMessengerEXT)
			{
				VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
				info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				info.pfnUserCallback = DebugCallback;
				vk.CreateDebugUtilsMessengerEXT(vk.Instance, &info, nullptr, &vk.Messenger);
			}
		}

		// ---- Surface ----
		if(desc.Window)
		{
			if(glfwCreateWindowSurface(vk.Instance, desc.Window, nullptr, &vk.Surface) != VK_SUCCESS)
			{
				SF_CORE_ERROR("Failed to create the window surface");
				return false;
			}
		}

		// ---- Physical device ----
		uint32_t deviceCount = 0;
		vk.EnumeratePhysicalDevices(vk.Instance, &deviceCount, nullptr);
		if(deviceCount == 0)
		{
			SF_CORE_ERROR("No Vulkan capable GPU found");
			return false;
		}
		std::vector<VkPhysicalDevice> devices(deviceCount);
		vk.EnumeratePhysicalDevices(vk.Instance, &deviceCount, devices.data());

		int bestScore = -1;
		for(VkPhysicalDevice candidate : devices)
		{
			VkPhysicalDeviceProperties props;
			vk.GetPhysicalDeviceProperties(candidate, &props);
			if(props.apiVersion < VK_API_VERSION_1_2)
				continue;

			uint32_t familyCount = 0;
			vk.GetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			vk.GetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

			int family = -1;
			for(uint32_t i = 0; i < familyCount; i++)
			{
				constexpr VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
				if((families[i].queueFlags & required) != required)
					continue;
				if(vk.Surface)
				{
					VkBool32 present = VK_FALSE;
					vk.GetPhysicalDeviceSurfaceSupportKHR(candidate, i, vk.Surface, &present);
					if(!present)
						continue;
				}
				family = static_cast<int>(i);
				break;
			}
			if(family < 0)
				continue;

			int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 : 100;
			score += static_cast<int>(props.limits.maxImageDimension2D / 1024);
			if(score > bestScore)
			{
				bestScore = score;
				vk.PhysicalDevice = candidate;
				vk.QueueFamily = static_cast<uint32_t>(family);
				m_AdapterName = props.deviceName;
			}
		}
		if(!vk.PhysicalDevice)
		{
			SF_CORE_ERROR("No suitable Vulkan device (needs Vulkan 1.2, graphics+compute queue{0})", vk.Surface ? ", presentation support" : "");
			return false;
		}

		// ---- Device extensions ----
		uint32_t devExtCount = 0;
		vk.EnumerateDeviceExtensionProperties(vk.PhysicalDevice, nullptr, &devExtCount, nullptr);
		std::vector<VkExtensionProperties> availableDeviceExtensions(devExtCount);
		vk.EnumerateDeviceExtensionProperties(vk.PhysicalDevice, nullptr, &devExtCount, availableDeviceExtensions.data());

		if(vk.Surface)
		{
			if(!HasExtension(availableDeviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			{
				SF_CORE_ERROR("GPU does not support VK_KHR_swapchain");
				return false;
			}
			vk.DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		for(const char* optional : { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, "VK_KHR_portability_subset", VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME })
			if(HasExtension(availableDeviceExtensions, optional))
				vk.DeviceExtensions.push_back(optional);

		// ---- Features ----
		VkPhysicalDeviceVulkan13Features supported13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan11Features supported11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceFeatures2 supported{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		VkPhysicalDeviceProperties props;
		vk.GetPhysicalDeviceProperties(vk.PhysicalDevice, &props);
		supported.pNext = &supported11;
		supported11.pNext = &supported12;
		if(props.apiVersion >= VK_API_VERSION_1_3)
			supported12.pNext = &supported13;
		vk.GetPhysicalDeviceFeatures2(vk.PhysicalDevice, &supported);

		if(!supported12.timelineSemaphore)
		{
			SF_CORE_ERROR("GPU lacks timelineSemaphore support required by the renderer");
			return false;
		}

		VkPhysicalDeviceVulkan13Features enabled13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features enabled12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan11Features enabled11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceFeatures2 enabled{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		enabled.pNext = &enabled11;
		enabled11.pNext = &enabled12;
		if(props.apiVersion >= VK_API_VERSION_1_3)
			enabled12.pNext = &enabled13;

		const VkPhysicalDeviceFeatures& s = supported.features;
		VkPhysicalDeviceFeatures& f = enabled.features;
		f.samplerAnisotropy = s.samplerAnisotropy;
		f.fillModeNonSolid = s.fillModeNonSolid;
		f.depthClamp = s.depthClamp;
		f.depthBiasClamp = s.depthBiasClamp;
		f.independentBlend = s.independentBlend;
		f.imageCubeArray = s.imageCubeArray;
		f.textureCompressionBC = s.textureCompressionBC;
		f.shaderStorageImageReadWithoutFormat = s.shaderStorageImageReadWithoutFormat;
		f.shaderStorageImageWriteWithoutFormat = s.shaderStorageImageWriteWithoutFormat;
		f.multiDrawIndirect = s.multiDrawIndirect;
		f.wideLines = s.wideLines;
		f.geometryShader = s.geometryShader;
		f.shaderInt64 = s.shaderInt64;
		enabled11.shaderDrawParameters = supported11.shaderDrawParameters;
		enabled12.timelineSemaphore = VK_TRUE;
		enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
		enabled12.descriptorIndexing = supported12.descriptorIndexing;
		enabled12.runtimeDescriptorArray = supported12.runtimeDescriptorArray;
		enabled12.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
		enabled12.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
		enabled12.descriptorBindingSampledImageUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind;
		enabled12.descriptorBindingStorageBufferUpdateAfterBind = supported12.descriptorBindingStorageBufferUpdateAfterBind;
		enabled12.descriptorBindingUniformBufferUpdateAfterBind = supported12.descriptorBindingUniformBufferUpdateAfterBind;
		enabled12.descriptorBindingStorageImageUpdateAfterBind = supported12.descriptorBindingStorageImageUpdateAfterBind;
		enabled12.descriptorBindingUpdateUnusedWhilePending = supported12.descriptorBindingUpdateUnusedWhilePending;
		enabled12.descriptorBindingVariableDescriptorCount = supported12.descriptorBindingVariableDescriptorCount;
		enabled12.shaderFloat16 = supported12.shaderFloat16;
		enabled12.hostQueryReset = supported12.hostQueryReset;
		if(props.apiVersion >= VK_API_VERSION_1_3)
		{
			enabled13.synchronization2 = supported13.synchronization2;
			enabled13.dynamicRendering = supported13.dynamicRendering;
		}

		float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = vk.QueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;

		std::vector<const char*> deviceExtensionPtrs;
		for(const auto& e : vk.DeviceExtensions)
			deviceExtensionPtrs.push_back(e.c_str());

		VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceInfo.pNext = &enabled;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionPtrs.size());
		deviceInfo.ppEnabledExtensionNames = deviceExtensionPtrs.data();

		result = vk.CreateDevice(vk.PhysicalDevice, &deviceInfo, nullptr, &vk.Device);
		if(result != VK_SUCCESS)
		{
			SF_CORE_ERROR("vkCreateDevice failed ({0})", static_cast<int>(result));
			return false;
		}

		vk.DestroyDevice = vk.LoadDeviceFn<PFN_vkDestroyDevice>("vkDestroyDevice");
		vk.GetDeviceQueue = vk.LoadDeviceFn<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
		vk.DeviceWaitIdle = vk.LoadDeviceFn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
		vk.CreateSwapchainKHR = vk.LoadDeviceFn<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
		vk.DestroySwapchainKHR = vk.LoadDeviceFn<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR");
		vk.GetSwapchainImagesKHR = vk.LoadDeviceFn<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
		vk.AcquireNextImageKHR = vk.LoadDeviceFn<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
		vk.QueuePresentKHR = vk.LoadDeviceFn<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
		vk.CreateSemaphoreFn = vk.LoadDeviceFn<PFN_vkCreateSemaphore>("vkCreateSemaphore");
		vk.DestroySemaphoreFn = vk.LoadDeviceFn<PFN_vkDestroySemaphore>("vkDestroySemaphore");
		vk.GetDeviceQueue(vk.Device, vk.QueueFamily, 0, &vk.Queue);

		// ---- nvrhi ----
		std::vector<const char*> instanceExtPtrs;
		for(const auto& e : vk.InstanceExtensions)
			instanceExtPtrs.push_back(e.c_str());

		// nvrhi_vk is built as a static library, so the host initializes the shared vulkan.hpp dispatcher.
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk.Instance, vk.GetInstanceProcAddr, vk.Device);

		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = &s_MessageCallback;
		deviceDesc.instance = vk.Instance;
		deviceDesc.physicalDevice = vk.PhysicalDevice;
		deviceDesc.device = vk.Device;
		deviceDesc.graphicsQueue = vk.Queue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(vk.QueueFamily);
		deviceDesc.computeQueue = VK_NULL_HANDLE;
		deviceDesc.transferQueue = VK_NULL_HANDLE;
		deviceDesc.instanceExtensions = instanceExtPtrs.data();
		deviceDesc.numInstanceExtensions = instanceExtPtrs.size();
		deviceDesc.deviceExtensions = deviceExtensionPtrs.data();
		deviceDesc.numDeviceExtensions = deviceExtensionPtrs.size();
		deviceDesc.bufferDeviceAddressSupported = enabled12.bufferDeviceAddress == VK_TRUE;
		deviceDesc.descriptorBindingUniformBufferUpdateAfterBind = enabled12.descriptorBindingUniformBufferUpdateAfterBind == VK_TRUE;

		nvrhi::DeviceHandle nvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);
		if(!nvrhiDevice)
		{
			SF_CORE_ERROR("Failed to create the nvrhi Vulkan device");
			return false;
		}
		if(desc.EnableValidation)
			nvrhiDevice = nvrhi::validation::createValidationLayer(nvrhiDevice);
		m_NvrhiDevice = nvrhiDevice;

		SF_CORE_INFO("Vulkan device: {0}", m_AdapterName);

		if(desc.Window)
		{
			int w = 0, h = 0;
			glfwGetFramebufferSize(desc.Window, &w, &h);
			m_Width = static_cast<uint32_t>(std::max(w, 0));
			m_Height = static_cast<uint32_t>(std::max(h, 0));
			if(m_Width > 0 && m_Height > 0 && !CreateSwapchain())
				return false;
			if(!m_Swapchain)
				m_Swapchain = CreateScope<SwapchainState>(); // minimized at startup; created on first resize
		}
		return true;
	}

	bool GraphicsDevice::CreateSwapchain()
	{
		Vulkan& vk = *m_Vk;
		if(!m_Swapchain)
			m_Swapchain = CreateScope<SwapchainState>();
		SwapchainState& sc = *m_Swapchain;

		VkSurfaceCapabilitiesKHR caps;
		vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(vk.PhysicalDevice, vk.Surface, &caps);

		uint32_t formatCount = 0;
		vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.PhysicalDevice, vk.Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.PhysicalDevice, vk.Surface, &formatCount, formats.data());
		VkSurfaceFormatKHR format{};
		bool found = false;
		for(const auto& candidate : formats)
			if(candidate.format == VK_FORMAT_B8G8R8A8_UNORM && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				format = candidate;
				found = true;
			}
		if(!found)
		{
			SF_CORE_ERROR("Surface does not support B8G8R8A8_UNORM");
			return false;
		}

		uint32_t modeCount = 0;
		vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.PhysicalDevice, vk.Surface, &modeCount, nullptr);
		std::vector<VkPresentModeKHR> modes(modeCount);
		vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.PhysicalDevice, vk.Surface, &modeCount, modes.data());
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if(!m_VSync)
		{
			for(VkPresentModeKHR mode : modes)
				if(mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
					presentMode = mode;
			for(VkPresentModeKHR mode : modes)
				if(mode == VK_PRESENT_MODE_MAILBOX_KHR)
					presentMode = mode;
		}

		VkExtent2D extent = caps.currentExtent;
		if(extent.width == 0xFFFFFFFFu)
		{
			extent.width = std::clamp(m_Width, caps.minImageExtent.width, caps.maxImageExtent.width);
			extent.height = std::clamp(m_Height, caps.minImageExtent.height, caps.maxImageExtent.height);
		}
		if(extent.width == 0 || extent.height == 0)
			return true; // minimized: nothing to create yet
		m_Width = extent.width;
		m_Height = extent.height;

		uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
		if(caps.maxImageCount > 0)
			imageCount = std::min(imageCount, caps.maxImageCount);

		VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		info.surface = vk.Surface;
		info.minImageCount = imageCount;
		info.imageFormat = format.format;
		info.imageColorSpace = format.colorSpace;
		info.imageExtent = extent;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = caps.currentTransform;
		info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		info.presentMode = presentMode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = VK_NULL_HANDLE;

		if(vk.CreateSwapchainKHR(vk.Device, &info, nullptr, &sc.Swapchain) != VK_SUCCESS)
		{
			SF_CORE_ERROR("vkCreateSwapchainKHR failed");
			return false;
		}

		uint32_t count = 0;
		vk.GetSwapchainImagesKHR(vk.Device, sc.Swapchain, &count, nullptr);
		sc.Images.resize(count);
		vk.GetSwapchainImagesKHR(vk.Device, sc.Swapchain, &count, sc.Images.data());

		for(VkImage image : sc.Images)
		{
			nvrhi::TextureDesc td;
			td.width = extent.width;
			td.height = extent.height;
			td.format = GetSwapchainFormat();
			td.debugName = "Swapchain";
			td.isRenderTarget = true;
			td.initialState = nvrhi::ResourceStates::Present;
			td.keepInitialState = true;
			nvrhi::TextureHandle texture = m_NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), td);
			sc.Textures.push_back(texture);
			sc.Framebuffers.push_back(m_NvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));

			VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VkSemaphore acquire = VK_NULL_HANDLE, present = VK_NULL_HANDLE;
			vk.CreateSemaphoreFn(vk.Device, &semInfo, nullptr, &acquire);
			vk.CreateSemaphoreFn(vk.Device, &semInfo, nullptr, &present);
			sc.AcquireSemaphores.push_back(acquire);
			sc.PresentSemaphores.push_back(present);
		}
		for(uint32_t i = 0; i < FramesInFlight; i++)
		{
			sc.FrameQueries[i] = m_NvrhiDevice->createEventQuery();
			sc.QuerySubmitted[i] = false;
		}
		sc.FrameIndex = 0;
		sc.FrameActive = false;
		return true;
	}

	void GraphicsDevice::DestroySwapchain()
	{
		if(!m_Swapchain || !m_Vk)
			return;
		SwapchainState& sc = *m_Swapchain;
		if(m_Vk->Device)
			m_Vk->DeviceWaitIdle(m_Vk->Device);
		sc.Framebuffers.clear();
		sc.Textures.clear();
		for(auto& q : sc.FrameQueries)
			q = nullptr;
		for(VkSemaphore s : sc.AcquireSemaphores)
			m_Vk->DestroySemaphoreFn(m_Vk->Device, s, nullptr);
		for(VkSemaphore s : sc.PresentSemaphores)
			m_Vk->DestroySemaphoreFn(m_Vk->Device, s, nullptr);
		sc.AcquireSemaphores.clear();
		sc.PresentSemaphores.clear();
		sc.Images.clear();
		if(sc.Swapchain)
			m_Vk->DestroySwapchainKHR(m_Vk->Device, sc.Swapchain, nullptr);
		sc.Swapchain = VK_NULL_HANDLE;
		sc.FrameActive = false;
	}

	void GraphicsDevice::Resize(uint32_t width, uint32_t height)
	{
		if(!m_Vk->Surface)
			return;
		if(width == m_Width && height == m_Height && m_Swapchain && m_Swapchain->Swapchain && !m_SwapchainDirty)
			return;
		m_Width = width;
		m_Height = height;
		DestroySwapchain();
		m_SwapchainDirty = false;
		if(width > 0 && height > 0)
			CreateSwapchain();
	}

	void GraphicsDevice::SetVSync(bool vsync)
	{
		if(m_VSync == vsync)
			return;
		m_VSync = vsync;
		m_SwapchainDirty = true;
	}

	bool GraphicsDevice::BeginFrame()
	{
		if(!m_Swapchain)
			return false;
		SwapchainState& sc = *m_Swapchain;

		if(m_SwapchainDirty || !sc.Swapchain)
		{
			int w = 0, h = 0;
			glfwGetFramebufferSize(m_Vk->Window, &w, &h);
			Resize(static_cast<uint32_t>(std::max(w, 0)), static_cast<uint32_t>(std::max(h, 0)));
		}
		if(!sc.Swapchain)
			return false; // minimized

		uint32_t slot = sc.FrameIndex % FramesInFlight;
		if(sc.QuerySubmitted[slot])
		{
			m_NvrhiDevice->waitEventQuery(sc.FrameQueries[slot]);
			m_NvrhiDevice->resetEventQuery(sc.FrameQueries[slot]);
			sc.QuerySubmitted[slot] = false;
		}

		VkSemaphore acquire = sc.AcquireSemaphores[sc.FrameIndex % sc.AcquireSemaphores.size()];
		VkResult result = m_Vk->AcquireNextImageKHR(m_Vk->Device, sc.Swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE, &sc.ImageIndex);
		if(result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_SwapchainDirty = true;
			return false;
		}
		if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			SF_CORE_ERROR("vkAcquireNextImageKHR failed ({0})", static_cast<int>(result));
			return false;
		}
		if(result == VK_SUBOPTIMAL_KHR)
			m_SwapchainDirty = true;

		VulkanDevice()->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acquire, 0);
		sc.FrameActive = true;
		return true;
	}

	void GraphicsDevice::Present()
	{
		if(!m_Swapchain || !m_Swapchain->FrameActive)
			return;
		SwapchainState& sc = *m_Swapchain;
		sc.FrameActive = false;

		VkSemaphore present = sc.PresentSemaphores[sc.ImageIndex];
		VulkanDevice()->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, present, 0);
		m_NvrhiDevice->executeCommandLists(nullptr, 0);

		VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &present;
		info.swapchainCount = 1;
		info.pSwapchains = &sc.Swapchain;
		info.pImageIndices = &sc.ImageIndex;
		VkResult result = m_Vk->QueuePresentKHR(m_Vk->Queue, &info);
		if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			m_SwapchainDirty = true;
		else if(result != VK_SUCCESS)
			SF_CORE_ERROR("vkQueuePresentKHR failed ({0})", static_cast<int>(result));

		uint32_t slot = sc.FrameIndex % FramesInFlight;
		m_NvrhiDevice->setEventQuery(sc.FrameQueries[slot], nvrhi::CommandQueue::Graphics);
		sc.QuerySubmitted[slot] = true;
		sc.FrameIndex++;
		m_NvrhiDevice->runGarbageCollection();
	}

	nvrhi::IFramebuffer* GraphicsDevice::GetCurrentFramebuffer() const
	{
		return m_Swapchain && m_Swapchain->FrameActive ? m_Swapchain->Framebuffers[m_Swapchain->ImageIndex].Get() : nullptr;
	}

	nvrhi::ITexture* GraphicsDevice::GetCurrentBackBuffer() const
	{
		return m_Swapchain && m_Swapchain->FrameActive ? m_Swapchain->Textures[m_Swapchain->ImageIndex].Get() : nullptr;
	}

	void GraphicsDevice::WaitIdle()
	{
		if(m_NvrhiDevice)
			m_NvrhiDevice->waitForIdle();
	}

	nvrhi::CommandListHandle GraphicsDevice::CreateCommandList()
	{
		return m_NvrhiDevice->createCommandList();
	}

	void GraphicsDevice::ExecuteAndWait(nvrhi::ICommandList* commandList)
	{
		m_NvrhiDevice->executeCommandList(commandList);
		m_NvrhiDevice->waitForIdle();
		m_NvrhiDevice->runGarbageCollection();
	}

}
