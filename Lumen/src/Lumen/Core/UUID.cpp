#include "Lumen/Core/UUID.h"

#include <mutex>
#include <random>

namespace Lumen {

	namespace {
		std::mutex s_Mutex;
		std::mt19937_64 s_Engine{ std::random_device{}() };
	}

	UUID::UUID()
	{
		std::lock_guard lock(s_Mutex);
		std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
		m_Value = dist(s_Engine);
	}

}
