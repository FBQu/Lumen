#include "Lumen/Core/Base64.h"

#include <array>

namespace Lumen::Base64 {

	namespace {
		constexpr char s_Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::array<int8_t, 256> MakeReverseTable()
		{
			std::array<int8_t, 256> table{};
			table.fill(-1);
			for (int i = 0; i < 64; i++)
				table[static_cast<unsigned char>(s_Alphabet[i])] = static_cast<int8_t>(i);
			return table;
		}
	}

	std::string Encode(const uint8_t* data, size_t size)
	{
		std::string out;
		out.reserve((size + 2) / 3 * 4);
		for (size_t i = 0; i < size; i += 3)
		{
			const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (i + 1 < size ? static_cast<uint32_t>(data[i + 1]) << 8 : 0u)
				| (i + 2 < size ? static_cast<uint32_t>(data[i + 2]) : 0u);
			out += s_Alphabet[(n >> 18) & 63];
			out += s_Alphabet[(n >> 12) & 63];
			out += i + 1 < size ? s_Alphabet[(n >> 6) & 63] : '=';
			out += i + 2 < size ? s_Alphabet[n & 63] : '=';
		}
		return out;
	}

	std::optional<std::vector<uint8_t>> Decode(std::string_view text)
	{
		static const std::array<int8_t, 256> table = MakeReverseTable();

		std::vector<uint8_t> out;
		out.reserve(text.size() / 4 * 3);
		uint32_t accumulator = 0;
		int bits = 0;
		size_t padding = 0;
		size_t symbols = 0;
		for (char c : text)
		{
			if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
				continue;
			if (c == '=')
			{
				padding++;
				continue;
			}
			if (padding > 0)
				return std::nullopt; // data after padding
			const int value = table[static_cast<unsigned char>(c)];
			if (value < 0)
				return std::nullopt;
			symbols++;
			accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFu));
			}
		}
		if (padding > 2 || (symbols + padding) % 4 != 0)
			return std::nullopt;
		if (padding == 0 && symbols % 4 != 0)
			return std::nullopt;
		return out;
	}

}
