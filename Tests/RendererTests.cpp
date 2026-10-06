#include <doctest/doctest.h>

#include "RenderTestUtil.h"

#include "Lumen/Renderer/Renderer.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <cmath>

using namespace Lumen;

namespace {

	constexpr uint32_t s_Size = 64;

	struct TestScene
	{
		Scene SceneData;
		Entity Camera;
		Entity Light;

		TestScene()
		{
			Camera = SceneData.CreateEntity("Camera");
			Camera.AddComponent<CameraComponent>();
			Camera.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 3.0f }; // looks down -Z at the origin

			Light = SceneData.CreateEntity("Sun"); // default rotation: shines along -Z, i.e. from the camera onto the scene
			Light.AddComponent<DirectionalLightComponent>();
		}

		Entity AddMesh(PrimitiveType type, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f))
		{
			Entity e = SceneData.CreateEntity("Mesh");
			e.GetComponent<TransformComponent>().Translation = position;
			e.GetComponent<TransformComponent>().Scale = scale;
			e.AddComponent<MeshRendererComponent>().Primitive = type;
			return e;
		}
	};

	Ref<Renderer> MakeRenderer(const Ref<RenderDevice>& device)
	{
		Ref<Renderer> renderer = Renderer::Create(device, s_Size, s_Size);
		REQUIRE(renderer != nullptr);
		renderer->GetSettings().Ambient = glm::vec3(0.0f);
		return renderer;
	}

	float Luminance(const uint8_t* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }

}

TEST_CASE("Renderer rejects invalid construction and invalid cameras")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	CHECK(Renderer::Create(nullptr, 64, 64) == nullptr);
	CHECK(Renderer::Create(device, 0, 64) == nullptr);

	Ref<Renderer> renderer = MakeRenderer(device);
	TestScene scene;
	CHECK_FALSE(renderer->Render(scene.SceneData, Entity{}));
	CHECK_FALSE(renderer->Render(scene.SceneData, scene.Light)); // no CameraComponent
}

TEST_CASE("Empty scene renders the tonemapped clear color")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	renderer->GetSettings().ClearColor = glm::vec3(1.0f, 0.0f, 0.0f);
	TestScene scene;
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));

	ImageData image = renderer->ReadOutput();
	REQUIRE(image.Width == s_Size);
	REQUIRE(image.Height == s_Size);
	const uint8_t* p = image.PixelAt(10, 40);
	CHECK(p[0] > 200); // ACES(1.0) is about 0.80 linear -> bright after sRGB encoding
	CHECK(p[1] < 5);
	CHECK(p[2] < 5);
	CHECK(p[3] == 255);
}

TEST_CASE("A lit sphere is drawn front-face-out in the middle of the frame")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	TestScene scene;
	scene.AddMesh(PrimitiveType::Sphere, { 0.0f, 0.0f, 0.0f });
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));

	ImageData image = renderer->ReadOutput();
	const float center = Luminance(image.PixelAt(s_Size / 2, s_Size / 2));
	const float corner = Luminance(image.PixelAt(2, 2));
	CHECK(center > 150.0f);  // lit by a light shining straight at it (would be dark if the sphere were inside-out)
	CHECK(corner < 60.0f);   // background
	CHECK(center > corner * 3.0f);

	// Lit sphere is symmetric about the vertical and horizontal axes.
	const float left = Luminance(image.PixelAt(s_Size / 2 - 8, s_Size / 2));
	const float right = Luminance(image.PixelAt(s_Size / 2 + 7, s_Size / 2));
	const float up = Luminance(image.PixelAt(s_Size / 2, s_Size / 2 - 8));
	const float down = Luminance(image.PixelAt(s_Size / 2, s_Size / 2 + 7));
	CHECK(std::abs(left - right) < 12.0f);
	CHECK(std::abs(up - down) < 12.0f);
}

TEST_CASE("Image orientation: light from the right lights the right side, light from above lights the top")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	TestScene scene;
	scene.AddMesh(PrimitiveType::Sphere, { 0.0f, 0.0f, 0.0f });
	const int cx = s_Size / 2, cy = s_Size / 2, r = 6; // the sphere is about 9 px in radius at this distance

	// Light travelling along -X comes from +X, which is the right of the screen for a camera looking down -Z.
	scene.Light.GetComponent<TransformComponent>().Rotation = { 0.0f, glm::half_pi<float>(), 0.0f }; // -Z rotated to -X
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData fromRight = renderer->ReadOutput();
	CHECK(Luminance(fromRight.PixelAt(cx + r, cy)) > Luminance(fromRight.PixelAt(cx - r, cy)) + 40.0f);

	// Light travelling along -Y comes from above: smaller pixel row = top of the image.
	scene.Light.GetComponent<TransformComponent>().Rotation = { -glm::half_pi<float>(), 0.0f, 0.0f }; // -Z rotated to -Y
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData fromAbove = renderer->ReadOutput();
	CHECK(Luminance(fromAbove.PixelAt(cx, cy - r)) > Luminance(fromAbove.PixelAt(cx, cy + r)) + 40.0f);
}

TEST_CASE("Depth testing: a nearer object hides a farther one regardless of draw order")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	TestScene scene;

	Entity farGreen = scene.AddMesh(PrimitiveType::Cube, { 0.0f, 0.0f, -1.0f }, { 3.0f, 3.0f, 1.0f }); // drawn first, behind
	farGreen.GetComponent<MeshRendererComponent>().Material.BaseColor = { 0.0f, 1.0f, 0.0f, 1.0f };
	Entity nearRed = scene.AddMesh(PrimitiveType::Cube, { 0.0f, 0.0f, 1.0f }, { 0.8f, 0.8f, 0.8f });
	nearRed.GetComponent<MeshRendererComponent>().Material.BaseColor = { 1.0f, 0.0f, 0.0f, 1.0f };

	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData image = renderer->ReadOutput();
	const uint8_t* center = image.PixelAt(s_Size / 2, s_Size / 2);
	CHECK(center[0] > center[1] + 60); // red cube in front (green only from the specular highlight)
	const uint8_t* edge = image.PixelAt(s_Size / 2 + 20, s_Size / 2);
	CHECK(edge[1] > edge[0] + 60); // green slab where the red cube does not cover it

	// Reverse creation order: same picture.
	Scene reversed;
	Entity camera = reversed.CreateEntity("Camera");
	camera.AddComponent<CameraComponent>();
	camera.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 3.0f };
	reversed.CreateEntity("Sun").AddComponent<DirectionalLightComponent>();
	Entity r1 = reversed.CreateEntity("Red");
	r1.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 1.0f };
	r1.GetComponent<TransformComponent>().Scale = { 0.8f, 0.8f, 0.8f };
	r1.AddComponent<MeshRendererComponent>().Material.BaseColor = { 1.0f, 0.0f, 0.0f, 1.0f };
	Entity g1 = reversed.CreateEntity("Green");
	g1.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, -1.0f };
	g1.GetComponent<TransformComponent>().Scale = { 3.0f, 3.0f, 1.0f };
	g1.AddComponent<MeshRendererComponent>().Material.BaseColor = { 0.0f, 1.0f, 0.0f, 1.0f };

	REQUIRE(renderer->Render(reversed, camera));
	ImageData image2 = renderer->ReadOutput();
	CHECK(image2.Pixels == image.Pixels);
}

TEST_CASE("Materials: metals tint reflections by base color, emissive glows without light")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);

	{
		TestScene scene;
		Entity gold = scene.AddMesh(PrimitiveType::Sphere, { 0.0f, 0.0f, 0.0f });
		gold.GetComponent<MeshRendererComponent>().Material = { { 1.0f, 0.05f, 0.05f, 1.0f }, 1.0f, 0.7f, { 0, 0, 0 } };
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		ImageData image = renderer->ReadOutput();
		const uint8_t* p = image.PixelAt(s_Size / 2, s_Size / 2);
		CHECK(p[0] > p[1] + 100); // red metal: the reflection is red, not white
		CHECK(p[0] > p[2] + 100);
	}

	{
		TestScene scene;
		scene.SceneData.DestroyEntity(scene.Light); // no light at all
		Entity lamp = scene.AddMesh(PrimitiveType::Cube, { 0.0f, 0.0f, 0.0f }, { 1.5f, 1.5f, 1.5f });
		lamp.GetComponent<MeshRendererComponent>().Material.BaseColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		lamp.GetComponent<MeshRendererComponent>().Material.Emissive = { 0.0f, 2.0f, 0.0f };
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		ImageData image = renderer->ReadOutput();
		const uint8_t* p = image.PixelAt(s_Size / 2, s_Size / 2);
		CHECK(p[1] > 150);
		CHECK(p[0] < 10);
		CHECK(p[2] < 10);
	}
}

TEST_CASE("Exposure scales brightness and rendering is deterministic")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	TestScene scene;
	scene.AddMesh(PrimitiveType::Sphere, { 0.0f, 0.0f, 0.0f });

	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData first = renderer->ReadOutput();
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	CHECK(renderer->ReadOutput().Pixels == first.Pixels);

	renderer->GetSettings().Exposure = 0.25f;
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData dark = renderer->ReadOutput();
	CHECK(Luminance(dark.PixelAt(s_Size / 2, s_Size / 2)) < Luminance(first.PixelAt(s_Size / 2, s_Size / 2)) - 30.0f);
}

TEST_CASE("The HDR target keeps radiance above 1 before tonemapping")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	TestScene scene;
	scene.Light.GetComponent<DirectionalLightComponent>().Intensity = 20.0f;
	scene.AddMesh(PrimitiveType::Sphere, { 0.0f, 0.0f, 0.0f });
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));

	ImageData hdr = device->ReadTexture(renderer->GetHdrTarget());
	REQUIRE(hdr.BytesPerPixel == 8);
	const uint16_t* p = reinterpret_cast<const uint16_t*>(hdr.PixelAt(s_Size / 2, s_Size / 2));
	const int exponent = (p[0] >> 10) & 0x1F; // half float exponent field; value >= 2 when exponent >= 16
	CHECK(exponent >= 16);

	ImageData ldr = renderer->ReadOutput();
	CHECK(ldr.PixelAt(s_Size / 2, s_Size / 2)[0] <= 255); // tonemapped output stays displayable
}
