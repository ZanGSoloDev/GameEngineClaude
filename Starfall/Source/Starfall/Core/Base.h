#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#if defined(_MSC_VER)
	#define SF_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
	#include <csignal>
	#define SF_DEBUGBREAK() std::raise(SIGTRAP)
#else
	#define SF_DEBUGBREAK() std::abort()
#endif

#ifdef SF_DEBUG
	#define SF_ENABLE_ASSERTS
#endif

#define SF_EXPAND_MACRO(x) x
#define SF_STRINGIFY_MACRO(x) #x
#define BIT(x) (1u << (x))
#define SF_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Starfall {

	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

}

#include "Starfall/Core/Log.h"
#include "Starfall/Core/Assert.h"
