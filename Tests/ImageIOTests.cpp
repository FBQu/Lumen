#include <doctest/doctest.h>

#include "Lumen/Assets/ImageIO.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Lumen;

namespace {

	ImageData MakeImage(uint32_t w, uint32_t h, uint32_t channels)
	{
		ImageData image;
		image.Width = w;
		image.Height = h;
		image.BytesPerPixel = channels;
		for (uint32_t y = 0; y < h; y++)
			for (uint32_t x = 0; x < w; x++)
				for (uint32_t c = 0; c < channels; c++)
					image.Pixels.push_back(static_cast<uint8_t>((x * 40 + y * 7 + c * 90) % 256));
		return image;
	}

	std::vector<uint8_t> ReadFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
	}

}

TEST_CASE("PNG round trip preserves RGBA pixels exactly")
{
	const std::string path = "imageio_roundtrip.png";
	ImageData original = MakeImage(7, 5, 4);

	std::string error;
	REQUIRE_MESSAGE(ImageIO::WritePNG(path, original, &error), error);

	std::vector<uint8_t> bytes = ReadFile(path);
	std::remove(path.c_str());
	REQUIRE(bytes.size() > 8);

	auto decoded = ImageIO::DecodeLDR(bytes.data(), bytes.size(), &error);
	REQUIRE_MESSAGE(decoded.has_value(), error);
	CHECK(decoded->Width == 7);
	CHECK(decoded->Height == 5);
	CHECK(decoded->BytesPerPixel == 4);
	CHECK(decoded->Pixels == original.Pixels);
}

TEST_CASE("Grayscale and RGB PNGs decode to RGBA")
{
	for (uint32_t channels : { 1u, 3u })
	{
		const std::string path = "imageio_channels.png";
		ImageData original = MakeImage(4, 4, channels);
		REQUIRE(ImageIO::WritePNG(path, original));
		std::vector<uint8_t> bytes = ReadFile(path);
		std::remove(path.c_str());

		auto decoded = ImageIO::DecodeLDR(bytes.data(), bytes.size());
		REQUIRE(decoded.has_value());
		CHECK(decoded->BytesPerPixel == 4);
		CHECK(decoded->PixelAt(0, 0)[3] == 255);
		if (channels == 1)
			CHECK(decoded->PixelAt(2, 1)[0] == decoded->PixelAt(2, 1)[2]); // gray replicated
		else
			CHECK(decoded->PixelAt(2, 1)[1] == original.PixelAt(2, 1)[1]);
	}
}

TEST_CASE("WritePNG validates its input")
{
	std::string error;
	CHECK_FALSE(ImageIO::WritePNG("never.png", ImageData{}, &error));
	CHECK_FALSE(error.empty());

	ImageData wrongSize = MakeImage(4, 4, 4);
	wrongSize.Pixels.pop_back();
	CHECK_FALSE(ImageIO::WritePNG("never.png", wrongSize));

	ImageData halfFloat = MakeImage(2, 2, 8); // 16-bit-per-channel RGBA is not supported
	CHECK_FALSE(ImageIO::WritePNG("never.png", halfFloat));

	CHECK_FALSE(ImageIO::WritePNG("/nonexistent-directory/x.png", MakeImage(2, 2, 4), &error));
	CHECK(error.find("could not write") != std::string::npos);
}

TEST_CASE("Decoding rejects garbage and empty input")
{
	std::string error;
	const uint8_t garbage[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	CHECK_FALSE(ImageIO::DecodeLDR(garbage, sizeof(garbage), &error).has_value());
	CHECK_FALSE(error.empty());
	CHECK_FALSE(ImageIO::DecodeLDR(nullptr, 0, &error).has_value());
	CHECK_FALSE(ImageIO::DecodeHDR(garbage, sizeof(garbage), &error).has_value());
	CHECK_FALSE(ImageIO::DecodeHDR(nullptr, 0).has_value());
}

TEST_CASE("Radiance HDR files decode to linear float values")
{
	// A 2x1 uncompressed RGBE image: (1,1,1) and (4,2,0.5).
	const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
	std::vector<uint8_t> data(header.begin(), header.end());
	for (uint8_t b : { 128, 128, 128, 129 }) data.push_back(b); // (0.5*2^1) = 1.0
	for (uint8_t b : { 128, 64, 16, 131 }) data.push_back(b);   // (0.5, 0.25, 0.0625) * 8 = (4, 2, 0.5)

	std::string error;
	auto image = ImageIO::DecodeHDR(data.data(), data.size(), &error);
	REQUIRE_MESSAGE(image.has_value(), error);
	CHECK(image->Width == 2);
	CHECK(image->Height == 1);
	REQUIRE(image->Pixels.size() == 8);
	CHECK(image->Pixels[0] == doctest::Approx(1.0f).epsilon(0.01));
	CHECK(image->Pixels[4] == doctest::Approx(4.0f).epsilon(0.01));
	CHECK(image->Pixels[5] == doctest::Approx(2.0f).epsilon(0.01));
	CHECK(image->Pixels[6] == doctest::Approx(0.5f).epsilon(0.02));
	CHECK(image->Pixels[7] == 1.0f); // alpha is filled in
}
