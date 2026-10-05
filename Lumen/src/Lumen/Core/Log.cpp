#include "Lumen/Core/Log.h"

#include <atomic>
#include <cstdio>

namespace Lumen {

	namespace {
		std::atomic<Log::Sink> s_Sink{ nullptr };
		std::atomic<LogLevel> s_MinLevel{ LogLevel::Trace };

		const char* LevelName(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace: return "TRACE";
				case LogLevel::Info:  return "INFO";
				case LogLevel::Warn:  return "WARN";
				case LogLevel::Error: return "ERROR";
				case LogLevel::Fatal: return "FATAL";
			}
			return "?";
		}
	}

	void Log::SetSink(Sink sink) { s_Sink = sink; }
	void Log::SetMinLevel(LogLevel level) { s_MinLevel = level; }

	void Log::Write(LogLevel level, std::string_view message)
	{
		if (level < s_MinLevel)
			return;

		if (Sink sink = s_Sink.load())
		{
			sink(level, message);
			return;
		}
		std::fprintf(stderr, "[%s] %.*s\n", LevelName(level), static_cast<int>(message.size()), message.data());
	}

}
