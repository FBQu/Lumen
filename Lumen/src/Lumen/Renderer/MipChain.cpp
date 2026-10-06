#include "Lumen/Renderer/MipChain.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Lumen {

	namespace {

		float SrgbToLinear(uint8_t value)
		{
			const float c = static_cast<float>(value) / 255.0f;
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		uint8_t LinearToSrgb(float value)
		{
			value = std::clamp(value, 0.0f, 1.0f);
			const float c = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::lround(c * 255.0f));
		}

		const std::array<float, 256>& SrgbTable()
		{
			static const std::array<float, 256> table = []
			{
				std::array<float, 256> t{};
				for (int i = 0; i < 256; i++)
					t[static_cast<size_t>(i)] = SrgbToLinear(static_cast<uint8_t>(i));
				return t;
			}();
			return table;
		}

	}

	std::vector<ImageData> GenerateMipChain(const ImageData& base, bool srgb)
	{
		std::vector<ImageData> chain;
		if (base.Width == 0 || base.Height == 0 || base.BytesPerPixel != 4
			|| base.Pixels.size() != static_cast<size_t>(base.Width) * base.Height * 4)
			return chain;

		chain.push_back(base);
		const std::array<float, 256>& table = SrgbTable();

		while (chain.back().Width > 1 || chain.back().Height > 1)
		{
			const ImageData& src = chain.back();
			ImageData dst;
			dst.Width = std::max(1u, src.Width / 2);
			dst.Height = std::max(1u, src.Height / 2);
			dst.BytesPerPixel = 4;
			dst.Pixels.resize(static_cast<size_t>(dst.Width) * dst.Height * 4);

			for (uint32_t y = 0; y < dst.Height; y++)
			{
				for (uint32_t x = 0; x < dst.Width; x++)
				{
					const uint32_t x0 = std::min(x * 2, src.Width - 1), x1 = std::min(x * 2 + 1, src.Width - 1);
					const uint32_t y0 = std::min(y * 2, src.Height - 1), y1 = std::min(y * 2 + 1, src.Height - 1);
					const uint8_t* taps[4] = { src.PixelAt(x0, y0), src.PixelAt(x1, y0), src.PixelAt(x0, y1), src.PixelAt(x1, y1) };

					uint8_t* out = &dst.Pixels[(static_cast<size_t>(y) * dst.Width + x) * 4];
					for (int c = 0; c < 4; c++)
					{
						if (srgb && c < 3)
						{
							float sum = 0.0f;
							for (const uint8_t* tap : taps)
								sum += table[tap[c]];
							out[c] = LinearToSrgb(sum * 0.25f);
						}
						else
						{
							int sum = 0;
							for (const uint8_t* tap : taps)
								sum += tap[c];
							out[c] = static_cast<uint8_t>((sum + 2) / 4);
						}
					}
				}
			}
			chain.push_back(std::move(dst));
		}
		return chain;
	}

}
