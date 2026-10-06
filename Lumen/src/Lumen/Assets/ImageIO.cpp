#include "Lumen/Assets/ImageIO.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#include <stb_image_write.h>

#include <limits>

namespace Lumen::ImageIO {

	namespace {
		void SetError(std::string* error, const std::string& message)
		{
			if (error)
				*error = message;
		}
	}

	bool WritePNG(const std::string& path, const ImageData& image, std::string* error)
	{
		if (image.Width == 0 || image.Height == 0 || image.BytesPerPixel < 1 || image.BytesPerPixel > 4
			|| image.Pixels.size() != static_cast<size_t>(image.Width) * image.Height * image.BytesPerPixel)
		{
			SetError(error, "image is empty or is not an 8-bit image with 1 to 4 channels");
			return false;
		}
		if (!stbi_write_png(path.c_str(), static_cast<int>(image.Width), static_cast<int>(image.Height), static_cast<int>(image.BytesPerPixel),
				image.Pixels.data(), static_cast<int>(image.Width * image.BytesPerPixel)))
		{
			SetError(error, "could not write '" + path + "'");
			return false;
		}
		return true;
	}

	std::optional<ImageData> DecodeLDR(const uint8_t* data, size_t size, std::string* error)
	{
		if (data == nullptr || size == 0 || size > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			SetError(error, "no image data");
			return std::nullopt;
		}

		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
		if (pixels == nullptr)
		{
			SetError(error, stbi_failure_reason() ? stbi_failure_reason() : "unknown decode error");
			return std::nullopt;
		}

		ImageData image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		image.BytesPerPixel = 4;
		image.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
		stbi_image_free(pixels);
		return image;
	}

	std::optional<HdrImage> DecodeHDR(const uint8_t* data, size_t size, std::string* error)
	{
		if (data == nullptr || size == 0 || size > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			SetError(error, "no image data");
			return std::nullopt;
		}

		int width = 0, height = 0, channels = 0;
		float* pixels = stbi_loadf_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
		if (pixels == nullptr)
		{
			SetError(error, stbi_failure_reason() ? stbi_failure_reason() : "unknown decode error");
			return std::nullopt;
		}

		HdrImage image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		image.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
		stbi_image_free(pixels);
		return image;
	}

}
