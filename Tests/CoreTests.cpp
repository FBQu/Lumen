#include <doctest/doctest.h>

#include "Lumen/Core/Log.h"
#include "Lumen/Core/UUID.h"

#include <string>
#include <unordered_set>
#include <vector>

using namespace Lumen;

namespace {
	std::vector<std::pair<LogLevel, std::string>> s_Captured;
	void CaptureSink(LogLevel level, std::string_view message) { s_Captured.emplace_back(level, std::string(message)); }
}

TEST_CASE("UUID is always valid and unique")
{
	std::unordered_set<UUID> seen;
	for (int i = 0; i < 10000; i++)
	{
		UUID id;
		CHECK(id.IsValid());
		CHECK(seen.insert(id).second);
	}
}

TEST_CASE("UUID zero is invalid and round-trips")
{
	CHECK_FALSE(UUID(0).IsValid());
	CHECK(static_cast<uint64_t>(UUID(42)) == 42);
	CHECK(UUID(42) == UUID(42));
}

TEST_CASE("Log routes formatted messages to the sink and honors min level")
{
	s_Captured.clear();
	Log::SetSink(CaptureSink);
	Log::SetMinLevel(LogLevel::Warn);

	LM_INFO("hidden {}", 1);
	LM_WARN("shown {} {}", "a", 2);
	LM_ERROR("plain");

	Log::SetMinLevel(LogLevel::Trace);
	Log::SetSink(nullptr);

	REQUIRE(s_Captured.size() == 2);
	CHECK(s_Captured[0].first == LogLevel::Warn);
	CHECK(s_Captured[0].second == "shown a 2");
	CHECK(s_Captured[1].first == LogLevel::Error);
}
