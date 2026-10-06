#pragma once

#include "Lumen/Renderer/RenderDevice.h"

#include <cstdlib>
#include <doctest/doctest.h>

// Shared headless Vulkan device for renderer tests. If no Vulkan device exists, GPU tests are skipped with a message,
// unless LUMEN_REQUIRE_GPU=1 is set (CI), in which case they fail.
inline Lumen::Ref<Lumen::RenderDevice> GetTestRenderDevice()
{
	static Lumen::Ref<Lumen::RenderDevice> s_Device = Lumen::RenderDevice::Create({ true });
	if (!s_Device)
	{
		const char* require = std::getenv("LUMEN_REQUIRE_GPU");
		if (require != nullptr && require[0] == '1')
			FAIL("No Vulkan device available but LUMEN_REQUIRE_GPU=1");
		MESSAGE("No Vulkan device available; skipping GPU test");
	}
	return s_Device;
}
