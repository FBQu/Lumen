#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lumen::Base64 {

	// Standard alphabet with '=' padding.
	std::string Encode(const uint8_t* data, size_t size);
	inline std::string Encode(const std::vector<uint8_t>& data) { return Encode(data.data(), data.size()); }

	// Strict decoder: accepts the standard alphabet with correct padding (whitespace is ignored); returns nullopt otherwise.
	std::optional<std::vector<uint8_t>> Decode(std::string_view text);

}
