#include <doctest/doctest.h>

#include "RenderTestUtil.h"

using namespace Lumen;

TEST_CASE("RenderDevice initializes headless and reports a device name")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	CHECK_FALSE(device->GetDeviceName().empty());
	CHECK(device->GetDevice() != nullptr);
}

TEST_CASE("Clearing a render target and reading it back returns the clear color")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	nvrhi::TextureHandle target = device->CreateRenderTarget(33, 17, nvrhi::Format::RGBA8_UNORM, "ClearTest"); // odd size on purpose
	REQUIRE(target != nullptr);

	device->ExecuteImmediate([&](nvrhi::ICommandList* commandList)
	{
		commandList->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.5f, 0.75f, 1.0f));
	});

	ImageData image = device->ReadTexture(target);
	REQUIRE(image.Width == 33);
	REQUIRE(image.Height == 17);
	REQUIRE(image.BytesPerPixel == 4);
	REQUIRE(image.Pixels.size() == 33u * 17u * 4u);

	for (uint32_t y = 0; y < image.Height; y++)
	{
		for (uint32_t x = 0; x < image.Width; x++)
		{
			const uint8_t* p = image.PixelAt(x, y);
			REQUIRE(std::abs(int(p[0]) - 64) <= 1);
			REQUIRE(std::abs(int(p[1]) - 128) <= 1);
			REQUIRE(std::abs(int(p[2]) - 191) <= 1);
			REQUIRE(p[3] == 255);
		}
	}
}

TEST_CASE("Float render targets keep HDR values above 1")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	nvrhi::TextureHandle target = device->CreateRenderTarget(4, 4, nvrhi::Format::RGBA16_FLOAT, "HdrTest");
	device->ExecuteImmediate([&](nvrhi::ICommandList* commandList)
	{
		commandList->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(8.0f, 0.5f, 0.0f, 1.0f));
	});

	ImageData image = device->ReadTexture(target);
	REQUIRE(image.BytesPerPixel == 8);

	auto halfToFloat = [](uint16_t h)
	{
		const int exponent = (h >> 10) & 0x1F;
		const float mantissa = float(h & 0x3FF);
		const float sign = (h & 0x8000) ? -1.0f : 1.0f;
		if (exponent == 0)
			return sign * std::ldexp(mantissa, -24);
		return sign * std::ldexp(1.0f + mantissa / 1024.0f, exponent - 15);
	};
	const uint16_t* p = reinterpret_cast<const uint16_t*>(image.PixelAt(2, 2));
	CHECK(halfToFloat(p[0]) == doctest::Approx(8.0f));
	CHECK(halfToFloat(p[1]) == doctest::Approx(0.5f));
}
