#include <doctest/doctest.h>

#include "Lumen/Core/Parallel.h"
#include "Lumen/Renderer/Environment.h"

#include <atomic>
#include <cmath>
#include <numeric>

using namespace Lumen;
using namespace Lumen::EnvironmentBuilder;

namespace {

	ImageIO::HdrImage UniformEnvironment(float value, uint32_t width = 64)
	{
		ImageIO::HdrImage image;
		image.Width = width;
		image.Height = width / 2;
		image.Pixels.assign(static_cast<size_t>(image.Width) * image.Height * 4, value);
		return image;
	}

	// Radiance depends only on the vertical direction: bright red above, dim blue below.
	ImageIO::HdrImage TopBottomEnvironment()
	{
		ImageIO::HdrImage image = UniformEnvironment(0.0f, 128);
		for (uint32_t y = 0; y < image.Height; y++)
			for (uint32_t x = 0; x < image.Width; x++)
			{
				float* p = &image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4];
				const bool top = y < image.Height / 2;
				p[0] = top ? 4.0f : 0.0f;
				p[1] = 0.0f;
				p[2] = top ? 0.0f : 1.0f;
				p[3] = 1.0f;
			}
		return image;
	}

	EnvironmentSettings FastSettings()
	{
		EnvironmentSettings settings;
		settings.SpecularSize = 32;
		settings.SpecularSamples = 64;
		return settings;
	}

}

TEST_CASE("ParallelFor visits every index exactly once")
{
	for (size_t count : { size_t(0), size_t(1), size_t(7), size_t(1000) })
	{
		std::vector<std::atomic<int>> hits(count);
		ParallelFor(count, [&](size_t i) { hits[i]++; });
		for (size_t i = 0; i < count; i++)
			CHECK(hits[i] == 1);
	}
}

TEST_CASE("Equirect mapping round-trips and agrees with the axes")
{
	CHECK(DirectionToEquirect({ 0, 1, 0 }).y == doctest::Approx(0.0f));
	CHECK(DirectionToEquirect({ 0, -1, 0 }).y == doctest::Approx(1.0f));
	CHECK(DirectionToEquirect({ 0, 0, -1 }).x == doctest::Approx(0.5f)); // -Z is the image center
	CHECK(DirectionToEquirect({ 0, 0, -1 }).y == doctest::Approx(0.5f));
	CHECK(DirectionToEquirect({ 1, 0, 0 }).x == doctest::Approx(0.75f));

	for (float u : { 0.05f, 0.3f, 0.5f, 0.77f, 0.98f })
		for (float v : { 0.1f, 0.4f, 0.5f, 0.9f })
		{
			const glm::vec3 d = EquirectToDirection({ u, v });
			CHECK(glm::length(d) == doctest::Approx(1.0f));
			const glm::vec2 back = DirectionToEquirect(d);
			CHECK(back.x == doctest::Approx(u).epsilon(1e-4));
			CHECK(back.y == doctest::Approx(v).epsilon(1e-4));
		}
}

TEST_CASE("Cube face directions: centers hit the axes, texels are unit length and faces tile the sphere")
{
	const glm::vec3 axes[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	for (uint32_t face = 0; face < 6; face++)
	{
		const glm::vec3 center = CubeFaceDirection(face, 0.5f, 0.5f);
		CHECK(glm::length(center - axes[face]) < 1e-5f);
		for (float u : { 0.1f, 0.9f })
			for (float v : { 0.2f, 0.8f })
			{
				const glm::vec3 d = CubeFaceDirection(face, u, v);
				CHECK(glm::length(d) == doctest::Approx(1.0f));
				CHECK(glm::dot(d, axes[face]) > 0.5f); // stays on its own face's side
			}
	}
	// Vulkan convention: on the +Z face u grows toward +X, v grows downward (-Y).
	CHECK(CubeFaceDirection(4, 0.9f, 0.5f).x > 0.0f);
	CHECK(CubeFaceDirection(4, 0.5f, 0.9f).y < 0.0f);
}

TEST_CASE("Uniform environment: irradiance and every specular mip equal the radiance")
{
	std::string error;
	auto env = FromEquirect(UniformEnvironment(2.0f), FastSettings(), &error);
	REQUIRE_MESSAGE(env.has_value(), error);

	for (glm::vec3 n : { glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::normalize(glm::vec3(1, 1, 1)), glm::vec3(0, 0, 1) })
	{
		const glm::vec3 e = EvaluateIrradiance(env->IrradianceSH, n);
		CHECK(e.r == doctest::Approx(2.0f).epsilon(0.02));
		CHECK(e.g == doctest::Approx(2.0f).epsilon(0.02));
	}

	REQUIRE(env->Specular.MipCount >= 4);
	CHECK(env->Specular.Size == 32);
	for (uint32_t mip = 0; mip < env->Specular.MipCount; mip++)
		for (uint32_t face = 0; face < 6; face++)
		{
			const uint32_t size = env->Specular.MipSize(mip);
			const float* t = env->Specular.Texel(mip, face, size / 2, size / 3);
			CHECK(t[0] == doctest::Approx(2.0f).epsilon(0.03));
			CHECK(t[3] == 1.0f);
		}
}

TEST_CASE("Top/bottom environment: irradiance follows the normal and roughness blurs reflections")
{
	std::string error;
	auto env = FromEquirect(TopBottomEnvironment(), FastSettings(), &error);
	REQUIRE_MESSAGE(env.has_value(), error);

	const glm::vec3 up = EvaluateIrradiance(env->IrradianceSH, { 0, 1, 0 });
	const glm::vec3 down = EvaluateIrradiance(env->IrradianceSH, { 0, -1, 0 });
	CHECK(up.r > 2.0f);   // red sky above
	CHECK(up.r > down.r * 5.0f);
	CHECK(down.b > up.b); // blue ground below
	const glm::vec3 side = EvaluateIrradiance(env->IrradianceSH, { 1, 0, 0 });
	CHECK(side.r == doctest::Approx(up.r * 0.5f).epsilon(0.25)); // a side-facing surface sees about half the sky

	const CubeMapData& cube = env->Specular;
	const uint32_t last = cube.MipCount - 1;
	// Straight up only ever sees sky (a surface samples the hemisphere around its normal), at every roughness.
	const float* upSharp = cube.Texel(0, 2, cube.MipSize(0) / 2, cube.MipSize(0) / 2);
	const float* upRough = cube.Texel(last, 2, cube.MipSize(last) / 2, cube.MipSize(last) / 2);
	CHECK(upSharp[0] == doctest::Approx(4.0f).epsilon(0.02));
	CHECK(upRough[0] > 3.4f);

	// At the horizon (+X face, just above and just below the center line) the sharp mip shows the hard edge,
	// while the roughest mip blends sky and ground.
	const uint32_t s0 = cube.MipSize(0), sl = cube.MipSize(last);
	const float* above = cube.Texel(0, 0, s0 / 2, s0 / 2 - 1);
	const float* below = cube.Texel(0, 0, s0 / 2, s0 / 2);
	CHECK(above[0] > 3.5f);
	CHECK(below[0] < 0.5f);
	const float* blurredAbove = cube.Texel(last, 0, sl / 2, sl / 2 - 1);
	const float* blurredBelow = cube.Texel(last, 0, sl / 2, sl / 2);
	CHECK(blurredAbove[0] < above[0] - 0.5f);
	CHECK(blurredBelow[0] > below[0] + 0.5f);
	CHECK(blurredAbove[0] > blurredBelow[0]); // still brighter on the sky side
}

TEST_CASE("A bright sun lights surfaces facing it and shows up in the matching reflection direction")
{
	SkySettings sky;
	sky.SunDirection = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)); // toward +Z
	sky.SunColor = glm::vec3(200.0f);
	sky.SunAngularRadius = 0.1f;
	sky.ZenithColor = sky.HorizonColor = sky.GroundColor = glm::vec3(0.1f);
	ImageIO::HdrImage image = MakeProceduralSky(sky);
	REQUIRE(image.Width == 512);
	REQUIRE(image.Height == 256);

	auto env = FromEquirect(image, FastSettings());
	REQUIRE(env.has_value());

	const glm::vec3 toward = EvaluateIrradiance(env->IrradianceSH, { 0, 0, 1 });
	const glm::vec3 away = EvaluateIrradiance(env->IrradianceSH, { 0, 0, -1 });
	CHECK(toward.r > away.r * 3.0f);

	// The prefiltered maps peak where the sun is (+Z face center) and are dimmer on the opposite face.
	const CubeMapData& cube = env->Specular;
	for (uint32_t mip : { 1u, 2u })
	{
		const float* facing = cube.Texel(mip, 4, cube.MipSize(mip) / 2, cube.MipSize(mip) / 2);
		const float* opposite = cube.Texel(mip, 5, cube.MipSize(mip) / 2, cube.MipSize(mip) / 2);
		CHECK(facing[0] > opposite[0] * 2.0f);
	}
}

TEST_CASE("Specular prefilter conserves energy for a sky: no mip is brighter than the sharpest peak or negative")
{
	auto env = FromEquirect(MakeProceduralSky(), FastSettings());
	REQUIRE(env.has_value());
	float peak = 0.0f;
	for (uint32_t face = 0; face < 6; face++)
		for (uint32_t y = 0; y < env->Specular.MipSize(0); y++)
			for (uint32_t x = 0; x < env->Specular.MipSize(0); x++)
				peak = std::max(peak, env->Specular.Texel(0, face, x, y)[0]);

	for (uint32_t mip = 1; mip < env->Specular.MipCount; mip++)
	{
		const uint32_t size = env->Specular.MipSize(mip);
		for (uint32_t face = 0; face < 6; face++)
			for (uint32_t y = 0; y < size; y++)
				for (uint32_t x = 0; x < size; x++)
				{
					const float* t = env->Specular.Texel(mip, face, x, y);
					REQUIRE(std::isfinite(t[0]));
					REQUIRE(t[0] >= 0.0f);
					REQUIRE(t[0] <= peak * 1.01f);
				}
	}
}

TEST_CASE("BRDF LUT: bounded, sensible limits")
{
	const uint32_t size = 32;
	const std::vector<float> lut = BuildBrdfLut(size, 256);
	REQUIRE(lut.size() == size * size * 2);

	for (size_t i = 0; i < lut.size(); i += 2)
	{
		CHECK(lut[i] >= 0.0f);
		CHECK(lut[i + 1] >= 0.0f);
		CHECK(lut[i] + lut[i + 1] <= 1.02f); // energy conservation: scale + bias <= 1
	}

	auto at = [&](uint32_t roughRow, uint32_t nvCol) { return glm::vec2(lut[(roughRow * size + nvCol) * 2], lut[(roughRow * size + nvCol) * 2 + 1]); };
	// Smooth surface seen head-on: almost all energy is the F0 term (scale ~ 1, bias ~ 0).
	CHECK(at(0, size - 1).x > 0.9f);
	CHECK(at(0, size - 1).y < 0.1f);
	// Grazing view of a smooth surface shifts weight to the Fresnel bias term.
	CHECK(at(0, 0).y > at(0, size - 1).y);
	// Rough surfaces lose energy: scale + bias drops with roughness at normal incidence.
	CHECK(at(size - 1, size - 1).x + at(size - 1, size - 1).y < at(0, size - 1).x + at(0, size - 1).y);
}

TEST_CASE("Procedural sky: gradient, ground and sun")
{
	SkySettings sky;
	sky.Width = 256;
	ImageIO::HdrImage image = MakeProceduralSky(sky);
	REQUIRE(image.Width == 256);
	REQUIRE(image.Height == 128);

	auto sample = [&](const glm::vec3& direction)
	{
		const glm::vec2 uv = DirectionToEquirect(direction);
		const uint32_t x = std::min(image.Width - 1, static_cast<uint32_t>(uv.x * static_cast<float>(image.Width)));
		const uint32_t y = std::min(image.Height - 1, static_cast<uint32_t>(uv.y * static_cast<float>(image.Height)));
		return glm::vec3(image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4], image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4 + 1],
		                 image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4 + 2]);
	};

	const glm::vec3 zenith = sample(glm::normalize(glm::vec3(0.01f, 1.0f, -0.2f))); // away from the sun
	CHECK(zenith.b > zenith.r); // blue sky overhead
	CHECK(sample(glm::normalize(glm::vec3(0.0f, -1.0f, 0.1f))).r < zenith.b); // ground is darker than the sky
	CHECK(sample(sky.SunDirection).r > 30.0f); // the sun is a hot spot (HDR values far above 1)
	CHECK(sample(-sky.SunDirection).r < 2.0f);
}

TEST_CASE("FromEquirect rejects bad input and validates settings")
{
	std::string error;
	CHECK_FALSE(FromEquirect(ImageIO::HdrImage{}, {}, &error).has_value());
	CHECK(error.find("empty") != std::string::npos);

	ImageIO::HdrImage inconsistent = UniformEnvironment(1.0f);
	inconsistent.Pixels.pop_back();
	CHECK_FALSE(FromEquirect(inconsistent, {}, &error).has_value());

	ImageIO::HdrImage nan = UniformEnvironment(1.0f);
	nan.Pixels[10] = std::nanf("");
	CHECK_FALSE(FromEquirect(nan, {}, &error).has_value());
	CHECK(error.find("NaN") != std::string::npos);

	EnvironmentSettings bad;
	bad.SpecularSize = 100; // not a power of two
	CHECK_FALSE(FromEquirect(UniformEnvironment(1.0f), bad, &error).has_value());
	CHECK(error.find("power of two") != std::string::npos);
	bad.SpecularSize = 4;
	CHECK_FALSE(FromEquirect(UniformEnvironment(1.0f), bad, &error).has_value());
}

TEST_CASE("Large backgrounds are downsampled to the configured maximum width")
{
	EnvironmentSettings settings = FastSettings();
	settings.MaxBackgroundWidth = 64;
	auto env = FromEquirect(UniformEnvironment(1.5f, 256), settings);
	REQUIRE(env.has_value());
	CHECK(env->Background.Width == 64);
	CHECK(env->Background.Height == 32);
	CHECK(env->Background.Pixels[0] == doctest::Approx(1.5f));

	settings.MaxBackgroundWidth = 4096;
	CHECK(FromEquirect(UniformEnvironment(1.5f, 256), settings)->Background.Width == 256); // never upsampled
}
