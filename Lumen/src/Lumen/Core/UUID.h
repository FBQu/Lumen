#pragma once

#include <cstdint>
#include <functional>

namespace Lumen {

	// 64-bit random identifier. Zero is reserved as "invalid".
	class UUID
	{
	public:
		UUID();
		explicit UUID(uint64_t value) : m_Value(value) {}

		explicit operator uint64_t() const { return m_Value; }
		bool IsValid() const { return m_Value != 0; }
		bool operator==(const UUID& other) const { return m_Value == other.m_Value; }
	private:
		uint64_t m_Value;
	};

}

template<>
struct std::hash<Lumen::UUID>
{
	size_t operator()(const Lumen::UUID& uuid) const { return std::hash<uint64_t>{}(static_cast<uint64_t>(uuid)); }
};
