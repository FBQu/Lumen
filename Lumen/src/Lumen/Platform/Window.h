#pragma once

#include "Lumen/Core/Base.h"
#include "Lumen/Core/Input.h"
#include "Lumen/Renderer/RenderDevice.h"

#include <string>

struct GLFWwindow;

namespace Lumen {

	// An OS window with a Vulkan swapchain, driven by GLFW. Creating a window also creates the process's RenderDevice
	// (with surface support), so it must be created before any headless RenderDevice.
	//
	// Frame loop:
	//     window->PollEvents();
	//     if (window->BeginFrame()) { render...; window->Present(texture); }
	class Window
	{
	public:
		struct Desc
		{
			std::string Title = "Lumen";
			uint32_t Width = 1280;
			uint32_t Height = 720;
			bool Resizable = true;
			bool VSync = true;
			bool Visible = true;
		};

		// Returns nullptr (after logging why) if there is no display or no Vulkan device that can present to it.
		static Ref<Window> Create(const Desc& desc);
		~Window();
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		const Ref<RenderDevice>& GetRenderDevice() const;
		GLFWwindow* GetNativeHandle() const;
		const InputState& GetInput() const;

		bool ShouldClose() const;
		void RequestClose();
		// Processes OS events and updates the input state (edges are cleared first).
		void PollEvents();

		// Size of the drawable area in pixels (0 while minimized).
		uint32_t GetWidth() const;
		uint32_t GetHeight() const;

		// Acquires the next back buffer. Returns false if nothing can be drawn this frame (minimized window).
		bool BeginFrame();
		nvrhi::IFramebuffer* GetBackbuffer() const; // valid between BeginFrame and EndFrame
		nvrhi::Format GetBackbufferFormat() const;

		// Draws `source` over the whole back buffer (bilinear) using the given command list.
		void DrawTexture(nvrhi::ICommandList* commandList, nvrhi::ITexture* source);
		// Submits the command list and presents the back buffer.
		void EndFrame(nvrhi::ICommandList* commandList);
		// Convenience: BeginFrame's counterpart that just shows `source`.
		void Present(nvrhi::ITexture* source);

		// When enabled, every presented frame is also copied to the CPU (see GetLastFrame). Useful for screenshots and tests.
		void SetCaptureEnabled(bool enabled);
		const ImageData& GetLastFrame() const; // RGBA8, top row first; empty until a frame was presented with capture on
	private:
		Window() = default;

		struct Impl;
		Scope<Impl> m_Impl;
	};

}
