#include "Lumen/Renderer/Environment.h"

#include "Lumen/Core/Parallel.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Lumen {

	size_t CubeMapData::Offset(uint32_t mip, uint32_t face) const
	{
		size_t offset = 0;
		for (uint32_t m = 0; m < mip; m++)
			offset += static_cast<size_t>(MipSize(m)) * MipSize(m) * 6 * 4;
		return offset + static_cast<size_t>(face) * MipSize(mip) * MipSize(mip) * 4;
	}

	const float* CubeMapData::Texel(uint32_t mip, uint32_t face, uint32_t x, uint32_t y) const
	{
		return &Pixels[Offset(mip, face) + (static_cast<size_t>(y) * MipSize(mip) + x) * 4];
	}

	namespace EnvironmentBuilder {

		namespace {

			constexpr float s_Pi = glm::pi<float>();

			// --- Equirectangular image pyramid ---------------------------------------------------------------------------
			struct Level
			{
				uint32_t Width, Height;
				std::vector<glm::vec3> Pixels;
			};

			std::vector<Level> BuildPyramid(const ImageIO::HdrImage& image)
			{
				std::vector<Level> levels;
				Level base{ image.Width, image.Height, {} };
				base.Pixels.resize(static_cast<size_t>(image.Width) * image.Height);
				for (size_t i = 0; i < base.Pixels.size(); i++)
					base.Pixels[i] = { image.Pixels[i * 4], image.Pixels[i * 4 + 1], image.Pixels[i * 4 + 2] };
				levels.push_back(std::move(base));

				while (levels.back().Width > 1 && levels.back().Height > 1)
				{
					const Level& src = levels.back();
					Level dst{ std::max(1u, src.Width / 2), std::max(1u, src.Height / 2), {} };
					dst.Pixels.resize(static_cast<size_t>(dst.Width) * dst.Height);
					for (uint32_t y = 0; y < dst.Height; y++)
					{
						for (uint32_t x = 0; x < dst.Width; x++)
						{
							const uint32_t x0 = std::min(x * 2, src.Width - 1), x1 = std::min(x * 2 + 1, src.Width - 1);
							const uint32_t y0 = std::min(y * 2, src.Height - 1), y1 = std::min(y * 2 + 1, src.Height - 1);
							dst.Pixels[static_cast<size_t>(y) * dst.Width + x] =
								(src.Pixels[static_cast<size_t>(y0) * src.Width + x0] + src.Pixels[static_cast<size_t>(y0) * src.Width + x1]
								 + src.Pixels[static_cast<size_t>(y1) * src.Width + x0] + src.Pixels[static_cast<size_t>(y1) * src.Width + x1]) * 0.25f;
						}
					}
					levels.push_back(std::move(dst));
				}
				return levels;
			}

			glm::vec3 SampleLevel(const Level& level, const glm::vec2& uv)
			{
				// Bilinear, wrapping in u and clamping in v. Texel centers sit at (i + 0.5) / size.
				const float fx = uv.x * static_cast<float>(level.Width) - 0.5f;
				const float fy = std::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(level.Height) - 0.5f;
				const float x0f = std::floor(fx), y0f = std::floor(fy);
				const float tx = fx - x0f, ty = fy - y0f;
				auto wrapX = [&](int x) { const int w = static_cast<int>(level.Width); return ((x % w) + w) % w; };
				auto clampY = [&](int y) { return std::clamp(y, 0, static_cast<int>(level.Height) - 1); };
				const int x0 = wrapX(static_cast<int>(x0f)), x1 = wrapX(static_cast<int>(x0f) + 1);
				const int y0 = clampY(static_cast<int>(y0f)), y1 = clampY(static_cast<int>(y0f) + 1);
				auto at = [&](int x, int y) { return level.Pixels[static_cast<size_t>(y) * level.Width + static_cast<size_t>(x)]; };
				return glm::mix(glm::mix(at(x0, y0), at(x1, y0), tx), glm::mix(at(x0, y1), at(x1, y1), tx), ty);
			}

			glm::vec3 SampleEquirect(const std::vector<Level>& pyramid, const glm::vec3& direction, float lod)
			{
				const glm::vec2 uv = DirectionToEquirect(direction);
				lod = std::clamp(lod, 0.0f, static_cast<float>(pyramid.size() - 1));
				const size_t lower = static_cast<size_t>(lod);
				const size_t upper = std::min(lower + 1, pyramid.size() - 1);
				const float t = lod - static_cast<float>(lower);
				const glm::vec3 a = SampleLevel(pyramid[lower], uv);
				return t > 0.0f && upper != lower ? glm::mix(a, SampleLevel(pyramid[upper], uv), t) : a;
			}

			// --- Sampling helpers ----------------------------------------------------------------------------------------
			float RadicalInverse(uint32_t bits)
			{
				bits = (bits << 16u) | (bits >> 16u);
				bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
				bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
				bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
				bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
				return static_cast<float>(bits) * 2.3283064365386963e-10f;
			}

			glm::vec2 Hammersley(uint32_t i, uint32_t count) { return { (static_cast<float>(i) + 0.5f) / static_cast<float>(count), RadicalInverse(i) }; }

			// Half vector in tangent space (z = normal) distributed according to GGX.
			glm::vec3 ImportanceSampleGGX(const glm::vec2& xi, float a)
			{
				const float phi = 2.0f * s_Pi * xi.x;
				const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
				const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
				return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
			}

			float DistributionGGX(float NoH, float a)
			{
				const float a2 = a * a;
				const float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
				return a2 / (s_Pi * d * d);
			}

			void BuildTangentFrame(const glm::vec3& n, glm::vec3& tangent, glm::vec3& bitangent)
			{
				const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
				tangent = glm::normalize(glm::cross(up, n));
				bitangent = glm::cross(n, tangent);
			}

			// --- Spherical harmonics -------------------------------------------------------------------------------------
			std::array<float, 9> ShBasis(const glm::vec3& d)
			{
				return { 0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y,
				         1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f), 1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y) };
			}

		}

		glm::vec2 DirectionToEquirect(const glm::vec3& d)
		{
			const float u = std::atan2(d.x, -d.z) / (2.0f * s_Pi) + 0.5f;
			const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / s_Pi;
			return { u, v };
		}

		glm::vec3 EquirectToDirection(const glm::vec2& uv)
		{
			const float phi = (uv.x - 0.5f) * 2.0f * s_Pi;
			const float theta = uv.y * s_Pi;
			return { std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi) };
		}

		glm::vec3 CubeFaceDirection(uint32_t face, float u, float v)
		{
			const float sc = 2.0f * u - 1.0f, tc = 2.0f * v - 1.0f;
			glm::vec3 d;
			switch (face)
			{
				case 0:  d = { 1.0f, -tc, -sc }; break;
				case 1:  d = { -1.0f, -tc, sc }; break;
				case 2:  d = { sc, 1.0f, tc }; break;
				case 3:  d = { sc, -1.0f, -tc }; break;
				case 4:  d = { sc, -tc, 1.0f }; break;
				default: d = { -sc, -tc, -1.0f }; break;
			}
			return glm::normalize(d);
		}

		glm::vec3 EvaluateIrradiance(const std::array<glm::vec3, 9>& sh, const glm::vec3& n)
		{
			const std::array<float, 9> basis = ShBasis(n);
			glm::vec3 result(0.0f);
			for (size_t i = 0; i < 9; i++)
				result += sh[i] * basis[i];
			return glm::max(result, glm::vec3(0.0f));
		}

		ImageIO::HdrImage MakeProceduralSky(const SkySettings& settings)
		{
			ImageIO::HdrImage image;
			image.Width = std::max(8u, settings.Width);
			image.Height = image.Width / 2;
			image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * 4);

			const glm::vec3 sun = glm::normalize(settings.SunDirection);
			const float cosSun = std::cos(settings.SunAngularRadius);
			for (uint32_t y = 0; y < image.Height; y++)
			{
				for (uint32_t x = 0; x < image.Width; x++)
				{
					const glm::vec3 d = EquirectToDirection({ (static_cast<float>(x) + 0.5f) / static_cast<float>(image.Width),
					                                          (static_cast<float>(y) + 0.5f) / static_cast<float>(image.Height) });
					glm::vec3 color = d.y >= 0.0f ? glm::mix(settings.HorizonColor, settings.ZenithColor, std::sqrt(d.y))
					                              : glm::mix(settings.HorizonColor, settings.GroundColor, std::sqrt(-d.y));
					if (glm::dot(d, sun) > cosSun)
						color += settings.SunColor;
					float* out = &image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4];
					out[0] = color.r;
					out[1] = color.g;
					out[2] = color.b;
					out[3] = 1.0f;
				}
			}
			return image;
		}

		std::vector<float> BuildBrdfLut(uint32_t size, uint32_t samples)
		{
			size = std::max(size, 1u);
			samples = std::max(samples, 1u);
			std::vector<float> lut(static_cast<size_t>(size) * size * 2);

			ParallelFor(size, [&](size_t row)
			{
				const float roughness = (static_cast<float>(row) + 0.5f) / static_cast<float>(size);
				const float a = roughness * roughness;
				const float k = a * 0.5f; // IBL remapping of the Smith-Schlick geometry term
				for (uint32_t col = 0; col < size; col++)
				{
					const float NoV = (static_cast<float>(col) + 0.5f) / static_cast<float>(size);
					const glm::vec3 V(std::sqrt(1.0f - NoV * NoV), 0.0f, NoV);
					float scale = 0.0f, bias = 0.0f;
					for (uint32_t i = 0; i < samples; i++)
					{
						const glm::vec3 H = ImportanceSampleGGX(Hammersley(i, samples), a);
						const glm::vec3 L = 2.0f * glm::dot(V, H) * H - V;
						const float NoL = std::max(L.z, 0.0f), NoH = std::max(H.z, 0.0f), VoH = std::max(glm::dot(V, H), 0.0f);
						if (NoL <= 0.0f)
							continue;
						const float G = (NoV / (NoV * (1.0f - k) + k)) * (NoL / (NoL * (1.0f - k) + k));
						const float gVis = G * VoH / std::max(NoH * NoV, 1e-5f);
						const float fc = std::pow(1.0f - VoH, 5.0f);
						scale += (1.0f - fc) * gVis;
						bias += fc * gVis;
					}
					float* out = &lut[(row * size + col) * 2];
					out[0] = scale / static_cast<float>(samples);
					out[1] = bias / static_cast<float>(samples);
				}
			});
			return lut;
		}

		std::optional<Environment> FromEquirect(const ImageIO::HdrImage& image, const EnvironmentSettings& settings, std::string* error)
		{
			auto fail = [error](const std::string& message) -> std::optional<Environment>
			{
				if (error)
					*error = message;
				return std::nullopt;
			};

			if (image.Width < 2 || image.Height < 2 || image.Pixels.size() != static_cast<size_t>(image.Width) * image.Height * 4)
				return fail("environment image is empty or has inconsistent dimensions");
			for (float value : image.Pixels)
				if (!std::isfinite(value))
					return fail("environment image contains NaN or infinite values");
			const uint32_t size = settings.SpecularSize;
			if (size < 8 || (size & (size - 1)) != 0)
				return fail("SpecularSize must be a power of two >= 8");

			Environment env;

			// Background: box-downsample very large images.
			ImageIO::HdrImage background = image;
			while (background.Width > std::max(settings.MaxBackgroundWidth, 2u) && background.Height >= 2)
			{
				ImageIO::HdrImage half;
				half.Width = background.Width / 2;
				half.Height = background.Height / 2;
				half.Pixels.resize(static_cast<size_t>(half.Width) * half.Height * 4);
				for (uint32_t y = 0; y < half.Height; y++)
					for (uint32_t x = 0; x < half.Width; x++)
						for (int c = 0; c < 4; c++)
						{
							auto at = [&](uint32_t px, uint32_t py) { return background.Pixels[(static_cast<size_t>(py) * background.Width + px) * 4 + static_cast<size_t>(c)]; };
							half.Pixels[(static_cast<size_t>(y) * half.Width + x) * 4 + static_cast<size_t>(c)] =
								0.25f * (at(x * 2, y * 2) + at(x * 2 + 1, y * 2) + at(x * 2, y * 2 + 1) + at(x * 2 + 1, y * 2 + 1));
						}
				background = std::move(half);
			}
			env.Background = std::move(background);

			const std::vector<Level> pyramid = BuildPyramid(env.Background);

			// Diffuse irradiance as 9 SH coefficients, integrated over the equirect pixels weighted by solid angle.
			const Level& sourceLevel = pyramid[std::min<size_t>(pyramid.size() - 1, pyramid.size() > 6 ? pyramid.size() - 6 : 0)]; // <= ~64..256 px wide is plenty for low frequencies
			std::array<glm::dvec3, 9> sh{};
			for (uint32_t y = 0; y < sourceLevel.Height; y++)
			{
				const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(sourceLevel.Height);
				const double solidAngle = (2.0 * s_Pi / sourceLevel.Width) * (s_Pi / sourceLevel.Height) * std::sin(static_cast<double>(v) * s_Pi);
				for (uint32_t x = 0; x < sourceLevel.Width; x++)
				{
					const glm::vec3 d = EquirectToDirection({ (static_cast<float>(x) + 0.5f) / static_cast<float>(sourceLevel.Width), v });
					const std::array<float, 9> basis = ShBasis(d);
					const glm::vec3 radiance = sourceLevel.Pixels[static_cast<size_t>(y) * sourceLevel.Width + x];
					for (size_t i = 0; i < 9; i++)
						sh[i] += glm::dvec3(radiance) * static_cast<double>(basis[i]) * solidAngle;
				}
			}
			const float band[9] = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f }; // A_l / pi
			for (size_t i = 0; i < 9; i++)
				env.IrradianceSH[i] = glm::vec3(sh[i]) * band[i];

			// Specular: GGX-prefiltered cube map, one roughness per mip.
			CubeMapData& cube = env.Specular;
			cube.Size = size;
			uint32_t mips = 1;
			while ((size >> mips) >= 4)
				mips++;
			cube.MipCount = mips;
			size_t total = 0;
			for (uint32_t m = 0; m < mips; m++)
				total += static_cast<size_t>(cube.MipSize(m)) * cube.MipSize(m) * 6 * 4;
			cube.Pixels.assign(total, 1.0f);

			const float sourceTexelSolidAngle = 2.0f * s_Pi * s_Pi / (static_cast<float>(pyramid[0].Width) * static_cast<float>(pyramid[0].Height));
			const uint32_t sampleCount = std::max(settings.SpecularSamples, 1u);

			for (uint32_t mip = 0; mip < mips; mip++)
			{
				const uint32_t mipSize = cube.MipSize(mip);
				const float roughness = mips > 1 ? static_cast<float>(mip) / static_cast<float>(mips - 1) : 0.0f;
				const float a = roughness * roughness;
				const float texelSolidAngle = 4.0f * s_Pi / (6.0f * static_cast<float>(mipSize) * static_cast<float>(mipSize));

				ParallelFor(static_cast<size_t>(mipSize) * 6, [&](size_t job)
				{
					const uint32_t face = static_cast<uint32_t>(job / mipSize);
					const uint32_t y = static_cast<uint32_t>(job % mipSize);
					for (uint32_t x = 0; x < mipSize; x++)
					{
						const glm::vec3 N = CubeFaceDirection(face, (static_cast<float>(x) + 0.5f) / static_cast<float>(mipSize),
						                                      (static_cast<float>(y) + 0.5f) / static_cast<float>(mipSize));
						glm::vec3 color(0.0f);
						if (mip == 0)
						{
							// Mirror-like: just fetch the source at a footprint matching the cube texel.
							color = SampleEquirect(pyramid, N, 0.5f * std::log2(std::max(texelSolidAngle / sourceTexelSolidAngle, 1.0f)));
						}
						else
						{
							glm::vec3 T, B;
							BuildTangentFrame(N, T, B);
							float weight = 0.0f;
							for (uint32_t i = 0; i < sampleCount; i++)
							{
								const glm::vec3 H = ImportanceSampleGGX(Hammersley(i, sampleCount), a);
								const glm::vec3 worldH = T * H.x + B * H.y + N * H.z;
								const glm::vec3 L = 2.0f * glm::dot(N, worldH) * worldH - N; // V = N
								const float NoL = glm::dot(N, L);
								if (NoL <= 0.0f)
									continue;
								// Pick a pyramid level whose texel covers the solid angle this sample represents (reduces noise).
								const float NoH = std::max(H.z, 0.0f);
								const float pdf = DistributionGGX(NoH, a) * 0.25f; // D * NoH / (4 VoH) with V = N so VoH = NoH
								const float sampleSolidAngle = 1.0f / (static_cast<float>(sampleCount) * pdf + 1e-6f);
								const float lod = 0.5f * std::log2(std::max(sampleSolidAngle / sourceTexelSolidAngle, 1.0f)) + 1.0f;
								color += SampleEquirect(pyramid, L, lod) * NoL;
								weight += NoL;
							}
							color = weight > 0.0f ? color / weight : SampleEquirect(pyramid, N, 0.0f);
						}
						float* out = &cube.Pixels[cube.Offset(mip, face) + (static_cast<size_t>(y) * mipSize + x) * 4];
						out[0] = color.r;
						out[1] = color.g;
						out[2] = color.b;
						out[3] = 1.0f;
					}
				});
			}
			return env;
		}

	}

}
