#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Starfall {

	enum class LogLevel : uint8_t { Trace = 0, Info, Warn, Error, Critical };

	class Log
	{
	public:
		using Sink = std::function<void(LogLevel level, std::string_view channel, std::string_view message)>;

		static void Init();
		static void Shutdown();

		static void SetLevel(LogLevel level);
		static LogLevel GetLevel();

		// Additional sinks (editor console, tests). Returns a handle for RemoveSink.
		static uint32_t AddSink(Sink sink);
		static void RemoveSink(uint32_t handle);

		static void Write(LogLevel level, std::string_view channel, std::string_view message);

		template<typename... Args>
		static void Write(LogLevel level, std::string_view channel, std::format_string<Args...> fmt, Args&&... args)
		{
			if(level < GetLevel())
				return;
			Write(level, channel, std::vformat(fmt.get(), std::make_format_args(args...)));
		}
	};

}

// Hazel-style placeholders are "{0}", "{1}" which std::format supports natively.
#define SF_CORE_TRACE(...)    ::Starfall::Log::Write(::Starfall::LogLevel::Trace, "Core", __VA_ARGS__)
#define SF_CORE_INFO(...)     ::Starfall::Log::Write(::Starfall::LogLevel::Info, "Core", __VA_ARGS__)
#define SF_CORE_WARN(...)     ::Starfall::Log::Write(::Starfall::LogLevel::Warn, "Core", __VA_ARGS__)
#define SF_CORE_ERROR(...)    ::Starfall::Log::Write(::Starfall::LogLevel::Error, "Core", __VA_ARGS__)
#define SF_CORE_CRITICAL(...) ::Starfall::Log::Write(::Starfall::LogLevel::Critical, "Core", __VA_ARGS__)

#define SF_TRACE(...)    ::Starfall::Log::Write(::Starfall::LogLevel::Trace, "App", __VA_ARGS__)
#define SF_INFO(...)     ::Starfall::Log::Write(::Starfall::LogLevel::Info, "App", __VA_ARGS__)
#define SF_WARN(...)     ::Starfall::Log::Write(::Starfall::LogLevel::Warn, "App", __VA_ARGS__)
#define SF_ERROR(...)    ::Starfall::Log::Write(::Starfall::LogLevel::Error, "App", __VA_ARGS__)
#define SF_CRITICAL(...) ::Starfall::Log::Write(::Starfall::LogLevel::Critical, "App", __VA_ARGS__)
