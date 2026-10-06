#pragma once

#include "Lumen/Renderer/RenderDevice.h" // ImageData

#include <vector>

namespace Lumen {

	// Builds the full mip chain of an 8-bit RGBA image with a 2x2 box filter (odd sizes are handled by clamping), down to
	// 1x1. Level 0 is a copy of `base`. If `srgb` is true, color channels are averaged in linear light (alpha is always
	// linear). Returns an empty vector for images that are not 4 bytes per pixel or are empty.
	std::vector<ImageData> GenerateMipChain(const ImageData& base, bool srgb);

}
