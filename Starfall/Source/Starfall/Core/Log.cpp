#include "Starfall/Core/Base.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace Starfall {

	namespace {

		struct SinkEntry
		{
			uint32_t Handle;
			Log::Sink Callback;
		};

		std::mutex s_Mutex;
		std::vector<SinkEntry> s_Sinks;
		std::atomic<LogLevel> s_Level{ LogLevel::Trace };
		uint32_t s_NextHandle = 1;

		const char* LevelName(LogLevel level)
		{
			switch(level)
			{
				case LogLevel::Trace: return "TRACE";
				case LogLevel::Info: return "INFO";
				case LogLevel::Warn: return "WARN";
				case LogLevel::Error: return "ERROR";
				case LogLevel::Critical: return "CRIT";
			}
			return "?";
		}

	}

	void Log::Init()
	{
		std::scoped_lock lock(s_Mutex);
		s_Sinks.clear();
		s_NextHandle = 1;
	}

	void Log::Shutdown()
	{
		std::scoped_lock lock(s_Mutex);
		s_Sinks.clear();
	}

	void Log::SetLevel(LogLevel level) { s_Level = level; }
	LogLevel Log::GetLevel() { return s_Level; }

	uint32_t Log::AddSink(Sink sink)
	{
		std::scoped_lock lock(s_Mutex);
		uint32_t handle = s_NextHandle++;
		s_Sinks.push_back({ handle, std::move(sink) });
		return handle;
	}

	void Log::RemoveSink(uint32_t handle)
	{
		std::scoped_lock lock(s_Mutex);
		std::erase_if(s_Sinks, [handle](const SinkEntry& e) { return e.Handle == handle; });
	}

	void Log::Write(LogLevel level, std::string_view channel, std::string_view message)
	{
		if(level < GetLevel())
			return;

		std::vector<Sink> sinks;
		{
			std::scoped_lock lock(s_Mutex);
			for(const auto& entry : s_Sinks)
				sinks.push_back(entry.Callback);
			std::FILE* out = level >= LogLevel::Warn ? stderr : stdout;
			std::fprintf(out, "[%s] %.*s: %.*s\n", LevelName(level), (int)channel.size(), channel.data(), (int)message.size(), message.data());
			std::fflush(out);
		}
		for(auto& sink : sinks)
			sink(level, channel, message);
	}

}
