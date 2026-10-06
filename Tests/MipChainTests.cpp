#include <doctest/doctest.h>

#include "Lumen/Renderer/MipChain.h"

using namespace Lumen;

namespace {

	ImageData Make(uint32_t w, uint32_t h, std::initializer_list<uint8_t> rgba)
	{
		ImageData image;
		image.Width = w;
		image.Height = h;
		image.BytesPerPixel = 4;
		for (uint32_t i = 0; i < w * h; i++)
			image.Pixels.insert(image.Pixels.end(), rgba.begin(), rgba.end());
		return image;
	}

}

TEST_CASE("Mip chain has the expected level sizes, including odd and non-square sizes")
{
	auto chain = GenerateMipChain(Make(5, 3, { 1, 2, 3, 4 }), false);
	REQUIRE(chain.size() == 3);
	CHECK(chain[0].Width == 5);
	CHECK(chain[0].Height == 3);
	CHECK(chain[1].Width == 2);
	CHECK(chain[1].Height == 1);
	CHECK(chain[2].Width == 1);
	CHECK(chain[2].Height == 1);

	CHECK(GenerateMipChain(Make(1, 1, { 9, 9, 9, 9 }), true).size() == 1);
	CHECK(GenerateMipChain(Make(256, 128, { 0, 0, 0, 0 }), false).size() == 9);
	for (const ImageData& level : GenerateMipChain(Make(64, 32, { 7, 7, 7, 7 }), true))
		CHECK(level.Pixels.size() == size_t(level.Width) * level.Height * 4);
}

TEST_CASE("Constant images stay constant through every level")
{
	for (bool srgb : { false, true })
	{
		for (const ImageData& level : GenerateMipChain(Make(8, 8, { 10, 100, 200, 255 }), srgb))
		{
			const uint8_t* p = level.PixelAt(level.Width - 1, level.Height - 1);
			CHECK(std::abs(int(p[0]) - 10) <= 1);
			CHECK(std::abs(int(p[1]) - 100) <= 1);
			CHECK(std::abs(int(p[2]) - 200) <= 1);
			CHECK(p[3] == 255);
		}
	}
}

TEST_CASE("Averaging happens in linear light for sRGB data and plainly for linear data")
{
	ImageData checker = Make(2, 1, { 0, 0, 0, 255 });
	checker.Pixels[4] = checker.Pixels[5] = checker.Pixels[6] = 255; // second pixel white

	const auto linearChain = GenerateMipChain(checker, false);
	const uint8_t* linear = linearChain.back().Pixels.data();
	CHECK(linear[0] == 128);

	const auto srgbChain = GenerateMipChain(checker, true);
	const uint8_t* srgb = srgbChain.back().Pixels.data();
	CHECK(srgb[0] == 188); // 0.5 linear is 188 in sRGB
	CHECK(srgb[3] == 255); // alpha is never gamma-converted
}

TEST_CASE("Mip generation rejects unsupported images")
{
	CHECK(GenerateMipChain(ImageData{}, false).empty());
	ImageData threeChannel = Make(2, 2, { 1, 2, 3, 4 });
	threeChannel.BytesPerPixel = 3;
	CHECK(GenerateMipChain(threeChannel, false).empty());
	ImageData wrongSize = Make(2, 2, { 1, 2, 3, 4 });
	wrongSize.Pixels.pop_back();
	CHECK(GenerateMipChain(wrongSize, false).empty());
}
