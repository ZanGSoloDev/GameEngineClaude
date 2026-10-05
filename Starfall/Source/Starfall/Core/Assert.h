#pragma once

#include "Starfall/Core/Base.h"

#ifdef SF_ENABLE_ASSERTS
	#define SF_INTERNAL_ASSERT_IMPL(check, msg, ...) { if(!(check)) { SF_CORE_ERROR(msg, __VA_ARGS__); SF_DEBUGBREAK(); } }
	#define SF_INTERNAL_ASSERT_WITH_MSG(check, ...) SF_INTERNAL_ASSERT_IMPL(check, "Assertion failed: {0}", __VA_ARGS__)
	#define SF_INTERNAL_ASSERT_NO_MSG(check) SF_INTERNAL_ASSERT_IMPL(check, "Assertion '{0}' failed at {1}:{2}", SF_STRINGIFY_MACRO(check), __FILE__, __LINE__)
	#define SF_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
	#define SF_INTERNAL_ASSERT_GET_MACRO(...) SF_EXPAND_MACRO(SF_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, SF_INTERNAL_ASSERT_WITH_MSG, SF_INTERNAL_ASSERT_NO_MSG))
	#define SF_ASSERT(...) SF_EXPAND_MACRO(SF_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(__VA_ARGS__))
	#define SF_CORE_ASSERT(...) SF_ASSERT(__VA_ARGS__)
#else
	#define SF_ASSERT(...)
	#define SF_CORE_ASSERT(...)
#endif

// Verify is always evaluated and always checked (also in release): use for recoverable-invariant checks with side effects.
#define SF_VERIFY(check) do { if(!(check)) { SF_CORE_ERROR("Verify failed: '{0}' at {1}:{2}", SF_STRINGIFY_MACRO(check), __FILE__, __LINE__); } } while(false)
