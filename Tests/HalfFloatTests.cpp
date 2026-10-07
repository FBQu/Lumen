#include <doctest/doctest.h>

#include "Lumen/Renderer/HalfFloat.h"

#include <cmath>
#include <initializer_list>
#include <limits>

using namespace Lumen;

TEST_CASE("Half float: exact values and known bit patterns")
{
	CHECK(FloatToHalf(0.0f) == 0x0000);
	CHECK(FloatToHalf(-0.0f) == 0x8000);
	CHECK(FloatToHalf(1.0f) == 0x3C00);
	CHECK(FloatToHalf(-2.0f) == 0xC000);
	CHECK(FloatToHalf(0.5f) == 0x3800);
	CHECK(FloatToHalf(65504.0f) == 0x7BFF);
	CHECK(FloatToHalf(std::ldexp(1.0f, -14)) == 0x0400);  // smallest normal
	CHECK(FloatToHalf(std::ldexp(1.0f, -24)) == 0x0001);  // smallest denormal
	for (uint16_t h : { uint16_t(0x3C00), uint16_t(0xC000), uint16_t(0x3555), uint16_t(0x0001), uint16_t(0x03FF), uint16_t(0x7BFF) })
		CHECK(FloatToHalf(HalfToFloat(h)) == h);
}

TEST_CASE("Half float: every finite half survives a round trip")
{
	for (uint32_t h = 0; h < 0x10000; h++)
	{
		const uint16_t half = static_cast<uint16_t>(h);
		if ((half & 0x7C00) == 0x7C00)
			continue; // inf and NaN handled separately
		REQUIRE(FloatToHalf(HalfToFloat(half)) == half);
	}
}

TEST_CASE("Half float: rounding, saturation and special values")
{
	// Halfway between 1.0 (0x3C00) and the next half rounds to even; just above rounds up.
	const float step = std::ldexp(1.0f, -10);
	CHECK(FloatToHalf(1.0f + step * 0.5f) == 0x3C00);
	CHECK(FloatToHalf(1.0f + step * 0.5f + 1e-6f) == 0x3C01);
	CHECK(FloatToHalf(1.0f + step * 1.5f) == 0x3C02); // halfway between 0x3C01 and 0x3C02: even wins

	CHECK(FloatToHalf(1.0e9f) == 0x7BFF);   // finite overflow saturates
	CHECK(FloatToHalf(-1.0e9f) == 0xFBFF);
	CHECK(FloatToHalf(65519.0f) == 0x7BFF);
	CHECK(FloatToHalf(1.0e-10f) == 0x0000); // underflow to zero
	CHECK(FloatToHalf(std::numeric_limits<float>::infinity()) == 0x7C00);
	CHECK((FloatToHalf(std::nanf("")) & 0x7C00) == 0x7C00);
	CHECK((FloatToHalf(std::nanf("")) & 0x03FF) != 0);
	CHECK(std::isnan(HalfToFloat(0x7E00)));
	CHECK(std::isinf(HalfToFloat(0x7C00)));

	for (float v : { 0.1f, 3.14159f, 100.25f, 1234.5f, 0.0003f })
		CHECK(HalfToFloat(FloatToHalf(v)) == doctest::Approx(v).epsilon(1e-3));
}
