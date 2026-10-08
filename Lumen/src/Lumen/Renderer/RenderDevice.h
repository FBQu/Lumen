#pragma once

#include "Lumen/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Lumen {

	// CPU-side copy of a texture's first mip/slice, tightly packed (no row padding).
	struct ImageData
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		uint32_t BytesPerPixel = 0;
		std::vector<uint8_t> Pixels;

		const uint8_t* PixelAt(uint32_t x, uint32_t y) const { return &Pixels[(static_cast<size_t>(y) * Width + x) * BytesPerPixel]; }
	};

	// Owns the Vulkan instance/device and the nvrhi device on top of it. Works headless (no window or surface needed),
	// which is how the renderer is unit tested; swapchain support is layered on top by the windowing code.
	class RenderDevice
	{
	public:
		struct Desc
		{
			bool EnableValidation = false;
		};

		// Returns nullptr (after logging why) if no usable Vulkan device exists. Vulkan-Hpp's dispatcher is process-global, so
		// only one device exists at a time: while a previously created device is still alive, Create returns that same
		// device (and `desc` is ignored).
		static Ref<RenderDevice> Create(const Desc& desc);
		~RenderDevice();
		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;

		nvrhi::IDevice* GetDevice() const { return m_NvrhiDevice; }
		const std::string& GetDeviceName() const { return m_DeviceName; }

		nvrhi::TextureHandle CreateRenderTarget(uint32_t width, uint32_t height, nvrhi::Format format, const char* name);

		// Copies mip 0 / slice 0 of `texture` back to the CPU, waiting for all GPU work to finish first.
		ImageData ReadTexture(nvrhi::ITexture* texture);

		// Creates a command list, records via `record`, submits it and waits for completion.
		template<typename Fn>
		void ExecuteImmediate(Fn&& record)
		{
			nvrhi::CommandListHandle commandList = m_NvrhiDevice->createCommandList();
			commandList->open();
			record(commandList.Get());
			commandList->close();
			m_NvrhiDevice->executeCommandList(commandList);
			m_NvrhiDevice->waitForIdle();
		}
	private:
		RenderDevice() = default;
		static Ref<RenderDevice> CreateNew(const Desc& desc);

		struct Vulkan;
		Scope<Vulkan> m_Vulkan;
		nvrhi::DeviceHandle m_NvrhiDevice;
		std::string m_DeviceName;
	};

}
