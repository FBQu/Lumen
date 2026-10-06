#pragma once

#include "Lumen/Renderer/RenderDevice.h" // ImageData

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lumen::ImageIO {

	// Writes an 8-bit image (1 to 4 bytes per pixel) as PNG. Returns false and fills `error` on failure.
	bool WritePNG(const std::string& path, const ImageData& image, std::string* error = nullptr);

	// Encodes an 8-bit image (1 to 4 bytes per pixel) as PNG in memory. Returns an empty vector for invalid images.
	std::vector<uint8_t> EncodePNG(const ImageData& image);

	// Decodes PNG/JPEG/TGA/BMP/HDR data from memory into 8-bit RGBA (4 bytes per pixel) for LDR formats.
	std::optional<ImageData> DecodeLDR(const uint8_t* data, size_t size, std::string* error = nullptr);

	struct HdrImage
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<float> Pixels; // RGBA, 32-bit float per channel
	};

	// Decodes Radiance .hdr (or any stb-supported format) into linear float RGBA. Used for HDRI environment maps.
	std::optional<HdrImage> DecodeHDR(const uint8_t* data, size_t size, std::string* error = nullptr);

}
