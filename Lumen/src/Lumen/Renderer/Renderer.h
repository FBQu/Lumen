#pragma once

#include "Lumen/Core/Base.h"
#include "Lumen/Renderer/RenderDevice.h"
#include "Lumen/Scene/Entity.h"

#include <glm/glm.hpp>

namespace Lumen {

	class Scene;

	// Forward PBR renderer: scene -> HDR (RGBA16F) -> ACES tonemap -> LDR (RGBA8, sRGB-encoded).
	class Renderer
	{
	public:
		struct Settings
		{
			float Exposure = 1.0f;
			glm::vec3 Ambient = { 0.03f, 0.03f, 0.03f }; // constant ambient radiance (replaced by IBL when available)
			glm::vec3 ClearColor = { 0.02f, 0.02f, 0.03f };
		};

		// Returns nullptr (after logging) if GPU resources cannot be created.
		static Ref<Renderer> Create(Ref<RenderDevice> device, uint32_t width, uint32_t height);
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		Settings& GetSettings();
		uint32_t GetWidth() const;
		uint32_t GetHeight() const;

		// Renders the scene from `camera` (an entity with CameraComponent and TransformComponent) into the output
		// texture and waits for completion. Returns false, drawing nothing, if the camera is invalid.
		bool Render(Scene& scene, Entity camera);

		nvrhi::ITexture* GetOutput() const;
		nvrhi::ITexture* GetHdrTarget() const;
		ImageData ReadOutput();
	private:
		Renderer() = default;

		struct Impl;
		Scope<Impl> m_Impl;
	};

}
