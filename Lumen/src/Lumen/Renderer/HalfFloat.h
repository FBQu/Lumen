#pragma once

#include <cstdint>
#include <cstring>

namespace Lumen {

	// IEEE 754 binary16 conversion (round to nearest even). Finite values beyond the half range saturate to +-65504 instead of
	// becoming infinity, which keeps very bright HDR pixels (e.g. the sun) finite on the GPU. NaN stays NaN.
	inline uint16_t FloatToHalf(float value)
	{
		uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		const uint32_t sign = (bits >> 16) & 0x8000u;
		const uint32_t exponentField = (bits >> 23) & 0xFFu;
		uint32_t mantissa = bits & 0x7FFFFFu;

		if (exponentField == 0xFFu)
			return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u)); // infinity or NaN (infinity is kept as is)

		const int32_t exponent = static_cast<int32_t>(exponentField) - 127 + 15;
		if (exponent >= 31)
			return static_cast<uint16_t>(sign | 0x7BFFu); // saturate to the largest finite half

		if (exponent <= 0)
		{
			if (exponent < -10)
				return static_cast<uint16_t>(sign); // too small even for a denormal
			mantissa |= 0x800000u;
			const uint32_t shift = static_cast<uint32_t>(14 - exponent);
			uint32_t half = mantissa >> shift;
			const uint32_t remainder = mantissa & ((1u << shift) - 1u);
			const uint32_t halfway = 1u << (shift - 1);
			if (remainder > halfway || (remainder == halfway && (half & 1u)))
				half++;
			return static_cast<uint16_t>(sign | half);
		}

		uint32_t half = (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
		const uint32_t remainder = mantissa & 0x1FFFu;
		if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u)))
			half++; // a carry into the exponent is correct; overflow to 0x7C00 is handled below
		if ((half & 0x7FFFu) >= 0x7C00u)
			half = 0x7BFFu;
		return static_cast<uint16_t>(sign | half);
	}

	inline float HalfToFloat(uint16_t half)
	{
		const uint32_t sign = (static_cast<uint32_t>(half) & 0x8000u) << 16;
		const uint32_t exponent = (half >> 10) & 0x1Fu;
		const uint32_t mantissa = half & 0x3FFu;
		uint32_t bits;
		if (exponent == 0)
		{
			if (mantissa == 0)
			{
				bits = sign;
			}
			else
			{
				// Denormal: normalize.
				uint32_t m = mantissa;
				int32_t e = -1;
				do { e++; m <<= 1; } while ((m & 0x400u) == 0);
				bits = sign | (static_cast<uint32_t>(127 - 15 - e) << 23) | ((m & 0x3FFu) << 13);
			}
		}
		else if (exponent == 31)
		{
			bits = sign | 0x7F800000u | (mantissa << 13);
		}
		else
		{
			bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
		}
		float result;
		std::memcpy(&result, &bits, sizeof(result));
		return result;
	}

}
