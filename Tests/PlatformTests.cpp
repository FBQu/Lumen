// Window and swapchain tests. They need a display (X server); on headless machines run them under Xvfb:
//     xvfb-run -a ./LumenPlatformTests
// Without a display they skip, unless LUMEN_REQUIRE_DISPLAY=1 is set (then they fail).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Lumen/Platform/Window.h"
#include "Lumen/Renderer/Renderer.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <GLFW/glfw3.h>

#include <cstdlib>

using namespace Lumen;

namespace {

	// One window for the whole test run: the render device is process-wide and tied to its surface.
	Ref<Window> GetTestWindow()
	{
		static Ref<Window> s_Window = Window::Create({ "Lumen platform test", 320, 240, true, true, true });
		if (!s_Window)
		{
			const char* require = std::getenv("LUMEN_REQUIRE_DISPLAY");
			if (require != nullptr && require[0] == '1')
				FAIL("No window could be created but LUMEN_REQUIRE_DISPLAY=1");
			MESSAGE("No display / presentable Vulkan device; skipping window test");
		}
		return s_Window;
	}

	float Luminance(const uint8_t* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }

	struct TestScene
	{
		Scene SceneData;
		Entity Camera;

		TestScene(bool withSphere)
		{
			Camera = SceneData.CreateEntity("Camera");
			Camera.AddComponent<CameraComponent>();
			Camera.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 3.0f };
			SceneData.CreateEntity("Sun").AddComponent<DirectionalLightComponent>();
			if (withSphere)
			{
				Entity sphere = SceneData.CreateEntity("Sphere");
				sphere.GetComponent<TransformComponent>().Scale = glm::vec3(1.5f);
				sphere.AddComponent<MeshRendererComponent>().Primitive = PrimitiveType::Sphere;
			}
		}
	};

	// Renders the scene offscreen and presents it; returns the captured back buffer.
	ImageData PresentScene(const Ref<Window>& window, const Ref<Renderer>& renderer, TestScene& scene)
	{
		window->SetCaptureEnabled(true);
		window->PollEvents();
		REQUIRE(window->BeginFrame());
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		window->Present(renderer->GetOutput());
		return window->GetLastFrame();
	}

}

TEST_CASE("Window opens with a presentable swapchain")
{
	auto window = GetTestWindow();
	if (!window)
		return;

	CHECK(window->GetRenderDevice() != nullptr);
	CHECK(window->GetRenderDevice()->GetVkSurface() != 0);
	CHECK_FALSE(window->ShouldClose());
	CHECK(window->GetWidth() > 0);
	CHECK(window->GetHeight() > 0);
	CHECK(window->GetBackbuffer() == nullptr); // only valid inside a frame

	REQUIRE(window->BeginFrame());
	CHECK(window->GetBackbuffer() != nullptr);
	nvrhi::CommandListHandle commandList = window->GetRenderDevice()->GetDevice()->createCommandList();
	commandList->open();
	commandList->clearTextureFloat(window->GetBackbuffer()->getDesc().colorAttachments[0].texture, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 1));
	commandList->close();
	window->EndFrame(commandList); // submitting a plain clear must present without errors
	CHECK(window->GetBackbuffer() == nullptr);
}

TEST_CASE("Presented frames show the rendered scene with correct colors (RGB order, upright)")
{
	auto window = GetTestWindow();
	if (!window)
		return;

	Ref<Renderer> renderer = Renderer::Create(window->GetRenderDevice(), 256, 192, 512);
	REQUIRE(renderer != nullptr);
	renderer->GetSettings().Ambient = glm::vec3(0.0f);
	renderer->GetSettings().EnableAmbientOcclusion = false;
	renderer->GetSettings().EnableShadows = false;

	// A red background: red must stay red (a BGRA/RGBA mix-up would turn it blue).
	renderer->GetSettings().ClearColor = glm::vec3(1.0f, 0.0f, 0.0f);
	TestScene empty(false);
	ImageData frame = PresentScene(window, renderer, empty);
	REQUIRE(frame.Width == window->GetWidth());
	REQUIRE(frame.Height == window->GetHeight());
	const uint8_t* p = frame.PixelAt(frame.Width / 2, frame.Height / 2);
	CHECK(p[0] > 180);
	CHECK(p[1] < 30);
	CHECK(p[2] < 30);

	// A sphere in the middle, light from the camera: bright center, dark corners.
	renderer->GetSettings().ClearColor = glm::vec3(0.0f, 0.0f, 0.02f);
	TestScene scene(true);
	frame = PresentScene(window, renderer, scene);
	CHECK(Luminance(frame.PixelAt(frame.Width / 2, frame.Height / 2)) > 100.0f);
	CHECK(Luminance(frame.PixelAt(4, 4)) < 40.0f);
	CHECK(Luminance(frame.PixelAt(frame.Width - 5, frame.Height - 5)) < 40.0f);

	// The picture is upright: light from above lights the top of the sphere more than the bottom.
	Entity sun = scene.SceneData.FindEntityByName("Sun");
	sun.GetComponent<TransformComponent>().Rotation = { -glm::half_pi<float>(), 0.0f, 0.0f };
	frame = PresentScene(window, renderer, scene);
	const uint32_t cx = frame.Width / 2, cy = frame.Height / 2, r = frame.Height / 6;
	CHECK(Luminance(frame.PixelAt(cx, cy - r)) > Luminance(frame.PixelAt(cx, cy + r)) + 30.0f);
}

TEST_CASE("The back buffer follows window resizes")
{
	auto window = GetTestWindow();
	if (!window)
		return;

	Ref<Renderer> renderer = Renderer::Create(window->GetRenderDevice(), 128, 128, 256);
	REQUIRE(renderer != nullptr);
	TestScene scene(true);

	glfwSetWindowSize(window->GetNativeHandle(), 400, 300);
	for (int i = 0; i < 100 && window->GetWidth() != 400; i++)
	{
		window->PollEvents();
		window->BeginFrame() ? window->Present(renderer->GetOutput()) : void();
		glfwWaitEventsTimeout(0.01);
	}
	ImageData frame = PresentScene(window, renderer, scene);
	CHECK(frame.Width == 400);
	CHECK(frame.Height == 300);
	CHECK(window->GetWidth() == 400);

	glfwSetWindowSize(window->GetNativeHandle(), 250, 180);
	for (int i = 0; i < 100 && window->GetWidth() != 250; i++)
	{
		window->PollEvents();
		window->BeginFrame() ? window->Present(renderer->GetOutput()) : void();
		glfwWaitEventsTimeout(0.01);
	}
	frame = PresentScene(window, renderer, scene);
	CHECK(frame.Width == 250);
	CHECK(frame.Height == 180);
	CHECK(Luminance(frame.PixelAt(frame.Width / 2, frame.Height / 2)) > 60.0f); // still rendering correctly after two recreations
}

TEST_CASE("Close requests and the per-frame input state")
{
	auto window = GetTestWindow();
	if (!window)
		return;

	window->PollEvents();
	CHECK_FALSE(window->GetInput().IsKeyDown(Key::W));
	CHECK(window->GetInput().GetScroll() == 0.0f);

	CHECK_FALSE(window->ShouldClose());
	window->RequestClose();
	CHECK(window->ShouldClose());
	glfwSetWindowShouldClose(window->GetNativeHandle(), GLFW_FALSE); // later tests keep using the window
	CHECK_FALSE(window->ShouldClose());
}
