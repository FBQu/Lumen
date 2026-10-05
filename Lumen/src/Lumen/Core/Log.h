#pragma once

#include <format>
#include <string>
#include <string_view>

namespace Lumen {

	enum class LogLevel { Trace, Info, Warn, Error, Fatal };

	class Log
	{
	public:
		using Sink = void(*)(LogLevel level, std::string_view message);

		static void SetSink(Sink sink);   // nullptr restores the default stderr sink
		static void SetMinLevel(LogLevel level);
		static void Write(LogLevel level, std::string_view message);

		template<typename... Args>
		static void Write(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
		{
			Write(level, std::string_view(std::format(fmt, std::forward<Args>(args)...)));
		}
	};

}

#define LM_TRACE(...) ::Lumen::Log::Write(::Lumen::LogLevel::Trace, __VA_ARGS__)
#define LM_INFO(...)  ::Lumen::Log::Write(::Lumen::LogLevel::Info,  __VA_ARGS__)
#define LM_WARN(...)  ::Lumen::Log::Write(::Lumen::LogLevel::Warn,  __VA_ARGS__)
#define LM_ERROR(...) ::Lumen::Log::Write(::Lumen::LogLevel::Error, __VA_ARGS__)
#define LM_FATAL(...) ::Lumen::Log::Write(::Lumen::LogLevel::Fatal, __VA_ARGS__)

#define LM_ASSERT(cond, ...) \
	do { if (!(cond)) { LM_FATAL("Assertion failed: " #cond " - " __VA_ARGS__); std::abort(); } } while (false)
