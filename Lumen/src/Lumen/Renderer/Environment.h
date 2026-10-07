#pragma once

#include "Lumen/Assets/ImageIO.h"

#include <glm/glm.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Lumen {

	// Cube map stored as RGBA32F on the CPU. Layout: mip-major, then face (+X, -X, +Y, -Y, +Z, -Z), then rows (top first).
	struct CubeMapData
	{
		uint32_t Size = 0;     // edge length of mip 0
		uint32_t MipCount = 0;
		std::vector<float> Pixels;

		uint32_t MipSize(uint32_t mip) const { return std::max(1u, Size >> mip); }
		size_t Offset(uint32_t mip, uint32_t face) const;
		const float* Texel(uint32_t mip, uint32_t face, uint32_t x, uint32_t y) const;
	};

	// Everything the renderer needs for image-based lighting, precomputed on the CPU from an equirectangular HDR image.
	struct Environment
	{
		ImageIO::HdrImage Background;              // equirectangular radiance for drawing the sky
		std::array<glm::vec3, 9> IrradianceSH;     // diffuse lighting: sum(coefficient_i * basis_i(n)) = irradiance / pi
		CubeMapData Specular;                      // GGX-prefiltered radiance, roughness = mip / (MipCount - 1)
	};

	struct EnvironmentSettings
	{
		uint32_t SpecularSize = 128;       // power of two, >= 8
		uint32_t SpecularSamples = 128;    // importance samples per texel of rougher mips
		uint32_t MaxBackgroundWidth = 2048; // the background is box-downsampled to at most this width
	};

	struct SkySettings
	{
		glm::vec3 ZenithColor = { 0.12f, 0.30f, 0.75f };
		glm::vec3 HorizonColor = { 0.75f, 0.82f, 0.92f };
		glm::vec3 GroundColor = { 0.20f, 0.19f, 0.18f };
		glm::vec3 SunDirection = glm::normalize(glm::vec3(0.4f, 0.6f, 0.3f)); // direction toward the sun
		glm::vec3 SunColor = { 40.0f, 36.0f, 30.0f };                          // radiance of the sun disc
		float SunAngularRadius = 0.04f;                                        // radians
		uint32_t Width = 512;                                                  // height is Width / 2
	};

	namespace EnvironmentBuilder
	{
		// Returns nullopt (and fills `error`) for empty, wrongly sized or non-finite images.
		std::optional<Environment> FromEquirect(const ImageIO::HdrImage& image, const EnvironmentSettings& settings = {}, std::string* error = nullptr);

		// A simple gradient sky with a sun disc, as an equirectangular HDR image.
		ImageIO::HdrImage MakeProceduralSky(const SkySettings& settings = {});

		// Split-sum BRDF lookup table, RG32F interleaved (scale, bias), `size` x `size`.
		// x = N.V in (0,1), y = roughness in (0,1) (both sampled at texel centers).
		std::vector<float> BuildBrdfLut(uint32_t size = 128, uint32_t samples = 128);

		// Diffuse outgoing radiance factor (irradiance / pi) in direction n.
		glm::vec3 EvaluateIrradiance(const std::array<glm::vec3, 9>& sh, const glm::vec3& n);

		// Equirectangular mapping shared with the shaders: u = atan2(x, -z) / 2pi + 0.5, v = acos(y) / pi (v = 0 is up).
		glm::vec2 DirectionToEquirect(const glm::vec3& direction);
		glm::vec3 EquirectToDirection(const glm::vec2& uv);

		// Direction through texel position (u, v) in [0,1]^2 of a cube face (v points down), using the Vulkan convention.
		glm::vec3 CubeFaceDirection(uint32_t face, float u, float v);
	}

}
