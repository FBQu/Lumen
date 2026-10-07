#include <doctest/doctest.h>

#include "RenderTestUtil.h"

#include "Lumen/Renderer/Renderer.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <array>
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

// ---------------------------------------------------------------------------------------------
// Asset-driven materials: textures, normal maps, alpha modes, double-sided, imported meshes
// ---------------------------------------------------------------------------------------------

#include "GltfTestData.h"
#include "Lumen/Assets/AssetManager.h"

#include <filesystem>
#include <fstream>

namespace {

	ImageData Texture2x2(const std::array<std::array<uint8_t, 4>, 4>& texels) // row-major: TL, TR, BL, BR
	{
		ImageData image;
		image.Width = image.Height = 2;
		image.BytesPerPixel = 4;
		for (const auto& texel : texels)
			image.Pixels.insert(image.Pixels.end(), texel.begin(), texel.end());
		return image;
	}

	ImageData Texture1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		ImageData image;
		image.Width = image.Height = 1;
		image.BytesPerPixel = 4;
		image.Pixels = { r, g, b, a };
		return image;
	}

	// A cube facing the camera, scaled so its front face covers most of the 64x64 frame.
	Entity AddBigCube(TestScene& scene, UUID materialAsset)
	{
		Entity cube = scene.AddMesh(PrimitiveType::Cube, { 0.0f, 0.0f, 0.0f }, { 1.6f, 1.6f, 0.2f });
		cube.GetComponent<MeshRendererComponent>().MaterialAsset = materialAsset;
		return cube;
	}

	MaterialAsset MatteWhite()
	{
		MaterialAsset material;
		material.Metallic = 0.0f;
		material.Roughness = 1.0f;
		return material;
	}

}

TEST_CASE("Base color textures map U to the right and V downwards, with the correct colors")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;
	MaterialAsset material = MatteWhite();
	material.BaseColorTexture = assets.AddTexture({ "Quadrants", Texture2x2({ { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 255, 255 } } }) });

	TestScene scene;
	AddBigCube(scene, assets.AddMaterial(material));
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
	ImageData image = renderer->ReadOutput();

	// The face spans about x,y = 17..47. Bilinear filtering makes a color pure only at the texel centers (u,v = 0.25/0.75).
	const uint8_t* topLeft = image.PixelAt(24, 24);
	const uint8_t* topRight = image.PixelAt(40, 24);
	const uint8_t* bottomLeft = image.PixelAt(24, 40);
	const uint8_t* bottomRight = image.PixelAt(40, 40);
	CHECK(topLeft[0] > topLeft[1] + 60);
	CHECK(topLeft[0] > topLeft[2] + 60);
	CHECK(topRight[1] > topRight[0] + 60);
	CHECK(topRight[1] > topRight[2] + 60);
	CHECK(bottomLeft[2] > bottomLeft[0] + 60);
	CHECK(bottomLeft[2] > bottomLeft[1] + 60);
	CHECK(bottomRight[0] > 120);
	CHECK(bottomRight[1] > 120);
	CHECK(bottomRight[2] > 120);
}

TEST_CASE("Metallic-roughness texture channels are read from G (roughness) and B (metallic)")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;

	auto render = [&](uint8_t g, uint8_t b)
	{
		MaterialAsset material = MatteWhite();
		material.BaseColor = { 1.0f, 0.0f, 0.0f, 1.0f };
		material.Metallic = 1.0f;
		material.Roughness = 1.0f;
		material.MetallicRoughnessTexture = assets.AddTexture({ "MR", Texture1x1(255, g, b, 255) });
		TestScene scene;
		scene.AddMesh(PrimitiveType::Sphere, { 0, 0, 0 }).GetComponent<MeshRendererComponent>().MaterialAsset = assets.AddMaterial(material);
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
		return renderer->ReadOutput();
	};

	// Same factors, different texture: B = 0 makes the red surface a dielectric (diffuse red lit), B = 255 a red metal.
	// A rough metal and a rough dielectric differ in how much white-ish specular they add (dielectric F0 is gray).
	ImageData dielectric = render(255, 0);
	ImageData metal = render(255, 255);
	const uint8_t* d = dielectric.PixelAt(s_Size / 2, s_Size / 2);
	const uint8_t* m = metal.PixelAt(s_Size / 2, s_Size / 2);
	CHECK(d[1] > m[1]); // dielectric reflects some non-red light, the red metal does not
	CHECK(d[0] > 100);
	CHECK(m[0] > 100);

	// Lower roughness (G) tightens the highlight at the center of a sphere lit head-on.
	ImageData rough = render(255, 255);
	ImageData smooth = render(40, 255);
	CHECK(smooth.PixelAt(s_Size / 2, s_Size / 2)[0] > rough.PixelAt(s_Size / 2, s_Size / 2)[0]);
}

TEST_CASE("Normal maps tilt the shading normal along the tangent (U) direction")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;

	auto render = [&](uint8_t r, uint8_t g, uint8_t b, float normalScale)
	{
		MaterialAsset material = MatteWhite();
		material.NormalTexture = assets.AddTexture({ "Normal", Texture1x1(r, g, b, 255) });
		material.NormalScale = normalScale;
		TestScene scene;
		scene.Light.GetComponent<TransformComponent>().Rotation = { 0.0f, glm::half_pi<float>(), 0.0f }; // light comes from +X, grazing the front face
		AddBigCube(scene, assets.AddMaterial(material));
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
		return Luminance(renderer->ReadOutput().PixelAt(s_Size / 2, s_Size / 2));
	};

	const float flat = render(128, 128, 255, 1.0f);   // unperturbed: light hits the face edge-on, so only ambient/none
	const float towardLight = render(255, 128, 200, 1.0f); // tilted toward +X, where the light is
	const float awayFromLight = render(0, 128, 200, 1.0f);
	CHECK(towardLight > flat + 40.0f);
	CHECK(awayFromLight <= flat + 5.0f);

	// Scale 0 removes the perturbation entirely.
	CHECK(std::abs(render(255, 128, 200, 0.0f) - flat) < 4.0f);
}

TEST_CASE("Alpha mask discards texels below the cutoff")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;
	MaterialAsset material = MatteWhite();
	material.Alpha = AlphaMode::Mask;
	material.AlphaCutoff = 0.5f;
	ImageData halfClear;
	halfClear.Width = 2;
	halfClear.Height = 1;
	halfClear.BytesPerPixel = 4;
	halfClear.Pixels = { 255, 255, 255, 0, 255, 255, 255, 255 }; // left transparent, right opaque
	material.BaseColorTexture = assets.AddTexture({ "Cutout", halfClear });

	TestScene scene;
	AddBigCube(scene, assets.AddMaterial(material));
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
	ImageData image = renderer->ReadOutput();
	CHECK(Luminance(image.PixelAt(22, 32)) < 60.0f);  // cut out: only the clear color remains
	CHECK(Luminance(image.PixelAt(42, 32)) > 100.0f); // opaque half is drawn
}

TEST_CASE("Alpha blending mixes with what is behind, sorted back to front")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);

	auto render = [&](float alpha)
	{
		TestScene scene;
		Entity wall = scene.AddMesh(PrimitiveType::Cube, { 0.0f, 0.0f, -1.0f }, { 4.0f, 4.0f, 0.2f });
		wall.GetComponent<MeshRendererComponent>().Material.BaseColor = { 0.0f, 1.0f, 0.0f, 1.0f };
		Entity glass = scene.AddMesh(PrimitiveType::Cube, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.1f });
		glass.GetComponent<MeshRendererComponent>().Material.BaseColor = { 1.0f, 0.0f, 0.0f, alpha };
		glass.GetComponent<MeshRendererComponent>().Material.Roughness = 1.0f; // matte, so no specular highlight adds green
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		return renderer->ReadOutput();
	};

	ImageData opaque = render(1.0f);
	ImageData blended = render(0.5f);
	const uint8_t* o = opaque.PixelAt(s_Size / 2, s_Size / 2);
	const uint8_t* b = blended.PixelAt(s_Size / 2, s_Size / 2);
	CHECK(o[0] > o[1] + 80);     // fully opaque red hides the green wall
	CHECK(b[0] > 60);            // translucent red is still visible...
	CHECK(b[1] > o[1] + 40);     // ...but the green wall shows through
}

TEST_CASE("Back faces are culled unless the material is double-sided")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;

	auto render = [&](bool doubleSided)
	{
		MaterialAsset material = MatteWhite();
		material.DoubleSided = doubleSided;
		TestScene scene;
		// Camera below the plane looking up; the plane's +Y normal faces away from it. The light shines upward too.
		scene.Camera.GetComponent<TransformComponent>().Translation = { 0.0f, -3.0f, 0.0f };
		scene.Camera.GetComponent<TransformComponent>().Rotation = { glm::half_pi<float>(), 0.0f, 0.0f };
		scene.Light.GetComponent<TransformComponent>().Rotation = { glm::half_pi<float>(), 0.0f, 0.0f };
		Entity plane = scene.AddMesh(PrimitiveType::Plane, { 0, 0, 0 }, { 6.0f, 1.0f, 6.0f });
		plane.GetComponent<MeshRendererComponent>().MaterialAsset = assets.AddMaterial(material);
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
		return Luminance(renderer->ReadOutput().PixelAt(s_Size / 2, s_Size / 2));
	};

	CHECK(render(false) < 60.0f);   // culled: background
	CHECK(render(true) > 100.0f);   // visible, and lit because the shading normal is flipped for back faces
}

TEST_CASE("Mesh and material assets render exactly like the equivalent primitive and inline material")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;

	TestScene inlineScene;
	Entity a = inlineScene.AddMesh(PrimitiveType::Cube, { 0, 0, 0 }, { 1.2f, 1.2f, 1.2f });
	a.GetComponent<MeshRendererComponent>().Material = { { 0.8f, 0.3f, 0.1f, 1.0f }, 0.2f, 0.4f, { 0, 0, 0 } };
	a.GetComponent<TransformComponent>().Rotation = { 0.3f, 0.5f, 0.0f };
	REQUIRE(renderer->Render(inlineScene.SceneData, inlineScene.Camera));
	ImageData expected = renderer->ReadOutput();

	MaterialAsset material;
	material.BaseColor = { 0.8f, 0.3f, 0.1f, 1.0f };
	material.Metallic = 0.2f;
	material.Roughness = 0.4f;
	TestScene assetScene;
	Entity b = assetScene.AddMesh(PrimitiveType::Sphere, { 0, 0, 0 }, { 1.2f, 1.2f, 1.2f }); // primitive is overridden by the asset
	b.GetComponent<TransformComponent>().Rotation = { 0.3f, 0.5f, 0.0f };
	b.GetComponent<MeshRendererComponent>().MeshAsset = assets.AddMesh({ "Cube", MeshGenerator::CreateCube() });
	b.GetComponent<MeshRendererComponent>().MaterialAsset = assets.AddMaterial(material);
	REQUIRE(renderer->Render(assetScene.SceneData, assetScene.Camera, &assets));
	CHECK(renderer->ReadOutput().Pixels == expected.Pixels);
}

TEST_CASE("Entities referencing missing assets are skipped; missing material falls back to the inline one")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;

	TestScene empty;
	REQUIRE(renderer->Render(empty.SceneData, empty.Camera, &assets));
	ImageData background = renderer->ReadOutput();

	TestScene scene;
	scene.AddMesh(PrimitiveType::Cube, { 0, 0, 0 }).GetComponent<MeshRendererComponent>().MeshAsset = UUID(987654321);
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
	CHECK(renderer->ReadOutput().Pixels == background.Pixels); // nothing drawn, no crash
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera, nullptr)); // no asset manager at all
	CHECK(renderer->ReadOutput().Pixels == background.Pixels);

	TestScene fallback;
	Entity cube = fallback.AddMesh(PrimitiveType::Sphere, { 0, 0, 0 });
	cube.GetComponent<MeshRendererComponent>().MaterialAsset = UUID(555); // unknown material
	REQUIRE(renderer->Render(fallback.SceneData, fallback.Camera, &assets));
	CHECK(Luminance(renderer->ReadOutput().PixelAt(s_Size / 2, s_Size / 2)) > 100.0f);
}

TEST_CASE("End to end: a glTF file is imported, instantiated and rendered")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "lumen_render_gltf";
	fs::create_directories(dir);
	const std::string path = (dir / "tri.gltf").string();
	std::string gltf = GltfTestData::TriangleGltf(); // red double-sided alpha-mask triangle covering x+y < 1 of the unit square
	const std::string emissive = R"("emissiveFactor":[0,1,0])";
	gltf.replace(gltf.find(emissive), emissive.size(), R"("emissiveFactor":[0,0,0])"); // keep it plainly red
	std::ofstream(path) << gltf;

	Ref<Renderer> renderer = MakeRenderer(device);
	AssetManager assets;
	std::string error;
	auto model = assets.ImportGltf(path, &error);
	fs::remove_all(dir);
	REQUIRE_MESSAGE(model.has_value(), error);

	TestScene scene;
	// Scale the model up and center it: the triangle spans (-1.5,-1.5), (1.5,-1.5), (-1.5,1.5) at z = 0.
	assets.Instantiate(scene.SceneData, *model, glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, -1.5f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(3.0f)));
	// The imported node carries its own translation (1,2,3) which we neutralize by moving the instance back.
	for (auto [handle, transform, mr] : scene.SceneData.GetRegistry().view<TransformComponent, MeshRendererComponent>().each())
		transform.Translation = { -1.5f, -1.5f, 0.0f };

	REQUIRE(renderer->Render(scene.SceneData, scene.Camera, &assets));
	ImageData image = renderer->ReadOutput();
	const uint8_t* inside = image.PixelAt(16, 46);  // lower left: inside the triangle
	const uint8_t* outside = image.PixelAt(50, 12); // upper right: beyond the hypotenuse
	CHECK(inside[0] > inside[1] + 60);
	CHECK(Luminance(outside) < 60.0f);
}

// ---------------------------------------------------------------------------------------------
// Image-based lighting
// ---------------------------------------------------------------------------------------------

#include "Lumen/Renderer/Environment.h"
#include "Lumen/Renderer/HalfFloat.h"

namespace {

	Ref<const Environment> BuildEnvironment(const ImageIO::HdrImage& image)
	{
		EnvironmentSettings settings;
		settings.SpecularSize = 64;
		settings.SpecularSamples = 128;
		std::string error;
		auto env = EnvironmentBuilder::FromEquirect(image, settings, &error);
		REQUIRE_MESSAGE(env.has_value(), error);
		return CreateRef<const Environment>(std::move(*env));
	}

	ImageIO::HdrImage UniformSky(float value)
	{
		ImageIO::HdrImage image;
		image.Width = 128;
		image.Height = 64;
		image.Pixels.assign(static_cast<size_t>(image.Width) * image.Height * 4, value);
		return image;
	}

	// Each direction gets the color of its dominant axis: +X red, -X cyan, +Y green, -Y magenta, +Z blue, -Z yellow.
	ImageIO::HdrImage AxisSky()
	{
		ImageIO::HdrImage image = UniformSky(1.0f);
		for (uint32_t y = 0; y < image.Height; y++)
			for (uint32_t x = 0; x < image.Width; x++)
			{
				const glm::vec3 d = EnvironmentBuilder::EquirectToDirection({ (x + 0.5f) / image.Width, (y + 0.5f) / image.Height });
				const glm::vec3 a = glm::abs(d);
				glm::vec3 color;
				if (a.x >= a.y && a.x >= a.z) color = d.x > 0 ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 1);
				else if (a.y >= a.z) color = d.y > 0 ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 1);
				else color = d.z > 0 ? glm::vec3(0, 0, 1) : glm::vec3(1, 1, 0);
				float* p = &image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4];
				p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = 1.0f;
			}
		return image;
	}

	glm::vec3 HdrPixel(const ImageData& hdr, uint32_t x, uint32_t y)
	{
		const uint16_t* p = reinterpret_cast<const uint16_t*>(hdr.PixelAt(x, y));
		return { HalfToFloat(p[0]), HalfToFloat(p[1]), HalfToFloat(p[2]) };
	}

	// A scene with only a camera (no directional light) so lighting comes purely from the environment.
	struct IblScene
	{
		Scene SceneData;
		Entity Camera;

		IblScene()
		{
			Camera = SceneData.CreateEntity("Camera");
			Camera.AddComponent<CameraComponent>();
			Camera.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 4.0f };
		}

		Entity AddSphere(const MaterialData& material, float diameter = 2.0f)
		{
			Entity e = SceneData.CreateEntity("Sphere");
			e.GetComponent<TransformComponent>().Scale = glm::vec3(diameter);
			auto& mr = e.AddComponent<MeshRendererComponent>();
			mr.Primitive = PrimitiveType::Sphere;
			mr.Material = material;
			return e;
		}
	};

}

TEST_CASE("White furnace: surfaces in a uniform white environment neither gain nor lose energy")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	REQUIRE(renderer->SetEnvironment(BuildEnvironment(UniformSky(1.0f))));
	CHECK(renderer->HasEnvironment());

	struct Case { float Metallic, Roughness; };
	for (const Case& c : { Case{ 0.0f, 0.2f }, Case{ 0.0f, 0.9f }, Case{ 1.0f, 0.2f }, Case{ 1.0f, 0.9f } })
	{
		IblScene scene;
		scene.AddSphere({ { 1.0f, 1.0f, 1.0f, 1.0f }, c.Metallic, c.Roughness, { 0, 0, 0 } });
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));

		const glm::vec3 value = HdrPixel(device->ReadTexture(renderer->GetHdrTarget()), s_Size / 2, s_Size / 2);
		INFO("metallic " << c.Metallic << " roughness " << c.Roughness << " -> " << value.r);
		// The split-sum approximation is not exactly energy conserving; stay within about 12% of the ideal 1.0.
		CHECK(value.r > 0.85f);
		CHECK(value.r < 1.08f);
		CHECK(value.r == doctest::Approx(value.g).epsilon(0.01));
	}
}

TEST_CASE("Environment directions: a mirror sphere reflects each axis color where it should")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	REQUIRE(renderer->SetEnvironment(BuildEnvironment(AxisSky())));
	renderer->GetSettings().ShowBackground = false;

	IblScene scene;
	scene.AddSphere({ { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, 0.0f, { 0, 0, 0 } }, 2.0f);
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData hdr = device->ReadTexture(renderer->GetHdrTarget());

	// The sphere's screen radius is about 18 pixels; sample at 45 degrees around the center, where the view
	// vector reflects to (almost exactly) the +/-X and +/-Y axes.
	const uint32_t c = s_Size / 2, off = 12;
	const glm::vec3 center = HdrPixel(hdr, c, c);
	const glm::vec3 right = HdrPixel(hdr, c + off, c);
	const glm::vec3 left = HdrPixel(hdr, c - off, c);
	const glm::vec3 top = HdrPixel(hdr, c, c - off);
	const glm::vec3 bottom = HdrPixel(hdr, c, c + off);

	CHECK(center.b > 0.6f); CHECK(center.r < 0.2f); CHECK(center.g < 0.2f);       // +Z blue (reflecting straight back at the camera)
	CHECK(right.r > 0.6f);  CHECK(right.g < 0.3f);  CHECK(right.b < 0.3f);       // +X red
	CHECK(left.g > 0.6f);   CHECK(left.b > 0.6f);   CHECK(left.r < 0.3f);        // -X cyan
	CHECK(top.g > 0.6f);    CHECK(top.r < 0.3f);    CHECK(top.b < 0.3f);         // +Y green
	CHECK(bottom.r > 0.6f); CHECK(bottom.b > 0.6f); CHECK(bottom.g < 0.3f);      // -Y magenta
}

TEST_CASE("Sky background: each camera direction shows the right part of the environment")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	REQUIRE(renderer->SetEnvironment(BuildEnvironment(AxisSky())));

	struct Look { glm::vec3 Rotation; glm::vec3 Expected; const char* Name; };
	const float h = glm::half_pi<float>();
	const Look looks[] = {
		{ { 0, 0, 0 }, { 1, 1, 0 }, "-Z yellow" },
		{ { 0, h, 0 }, { 0, 1, 1 }, "-X cyan" },
		{ { 0, -h, 0 }, { 1, 0, 0 }, "+X red" },
		{ { 0, glm::pi<float>(), 0 }, { 0, 0, 1 }, "+Z blue" },
		{ { h, 0, 0 }, { 0, 1, 0 }, "+Y green" },
		{ { -h, 0, 0 }, { 1, 0, 1 }, "-Y magenta" },
	};
	for (const Look& look : looks)
	{
		IblScene scene;
		scene.Camera.GetComponent<TransformComponent>().Rotation = look.Rotation;
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		const glm::vec3 value = HdrPixel(device->ReadTexture(renderer->GetHdrTarget()), s_Size / 2, s_Size / 2);
		INFO(look.Name << " -> " << value.r << ", " << value.g << ", " << value.b);
		CHECK(glm::length(value - look.Expected) < 0.15f);
	}
}

TEST_CASE("Environment intensity, background toggle and removal")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	renderer->GetSettings().ClearColor = glm::vec3(0.0f, 0.0f, 0.5f);
	renderer->GetSettings().Ambient = glm::vec3(0.25f);
	REQUIRE(renderer->SetEnvironment(BuildEnvironment(UniformSky(2.0f))));

	IblScene scene;
	auto centerValue = [&]
	{
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		return HdrPixel(device->ReadTexture(renderer->GetHdrTarget()), 3, 3); // a corner: pure background
	};

	CHECK(centerValue().r == doctest::Approx(2.0f).epsilon(0.01));
	renderer->GetSettings().EnvironmentIntensity = 0.5f;
	CHECK(centerValue().r == doctest::Approx(1.0f).epsilon(0.01));
	renderer->GetSettings().ShowBackground = false;
	CHECK(centerValue().b == doctest::Approx(0.5f).epsilon(0.01)); // clear color instead of the sky
	CHECK(centerValue().r == doctest::Approx(0.0f).epsilon(0.01));

	// Removing the environment goes back to the constant ambient term for lighting.
	REQUIRE(renderer->SetEnvironment(nullptr));
	CHECK_FALSE(renderer->HasEnvironment());
	IblScene lit;
	lit.AddSphere({ { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f, { 0, 0, 0 } });
	REQUIRE(renderer->Render(lit.SceneData, lit.Camera));
	CHECK(HdrPixel(device->ReadTexture(renderer->GetHdrTarget()), s_Size / 2, s_Size / 2).r == doctest::Approx(0.25f).epsilon(0.05));
}

TEST_CASE("Diffuse lighting follows the environment: sky-facing surfaces are brighter than ground-facing ones")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	ImageIO::HdrImage sky = UniformSky(0.0f);
	for (uint32_t y = 0; y < sky.Height / 2; y++)
		for (uint32_t x = 0; x < sky.Width; x++)
			for (int c = 0; c < 3; c++)
				sky.Pixels[(static_cast<size_t>(y) * sky.Width + x) * 4 + static_cast<size_t>(c)] = 3.0f;

	Ref<Renderer> renderer = MakeRenderer(device);
	REQUIRE(renderer->SetEnvironment(BuildEnvironment(sky)));
	IblScene scene;
	scene.AddSphere({ { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f, { 0, 0, 0 } });
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData hdr = device->ReadTexture(renderer->GetHdrTarget());

	const float top = HdrPixel(hdr, s_Size / 2, s_Size / 2 - 12).r;
	const float middle = HdrPixel(hdr, s_Size / 2, s_Size / 2).r;
	const float bottom = HdrPixel(hdr, s_Size / 2, s_Size / 2 + 12).r;
	CHECK(top > middle);
	CHECK(middle > bottom);
	CHECK(top > bottom * 3.0f);
}

TEST_CASE("Invalid environments are rejected without disturbing the current one")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeRenderer(device);
	CHECK_FALSE(renderer->HasEnvironment());
	CHECK_FALSE(renderer->SetEnvironment(CreateRef<const Environment>())); // empty
	CHECK_FALSE(renderer->HasEnvironment());

	REQUIRE(renderer->SetEnvironment(BuildEnvironment(UniformSky(1.0f))));
	CHECK_FALSE(renderer->SetEnvironment(CreateRef<const Environment>()));
	CHECK(renderer->HasEnvironment()); // still the previous environment
}

// ---------------------------------------------------------------------------------------------
// Shadows
// ---------------------------------------------------------------------------------------------

namespace {

	constexpr uint32_t s_ShadowFrame = 128;

	Ref<Renderer> MakeShadowRenderer(const Ref<RenderDevice>& device, uint32_t shadowMapSize = 1024)
	{
		Ref<Renderer> renderer = Renderer::Create(device, s_ShadowFrame, s_ShadowFrame, shadowMapSize);
		REQUIRE(renderer != nullptr);
		renderer->GetSettings().Ambient = glm::vec3(0.0f);
		return renderer;
	}

	// Camera 10 m above the origin looking straight down (screen right = +X, screen down = +Z); a large ground plane;
	// a sun travelling toward +X and downward, so shadows fall to the right (+X) of their casters.
	struct ShadowScene
	{
		Scene SceneData;
		Entity Camera, Sun, Ground, Cube;

		explicit ShadowScene(float cubeHeight = 1.0f, float cubeSize = 2.0f)
		{
			Camera = SceneData.CreateEntity("Camera");
			Camera.AddComponent<CameraComponent>();
			Camera.GetComponent<TransformComponent>().Translation = { 0.0f, 10.0f, 0.0f };
			Camera.GetComponent<TransformComponent>().Rotation = { -glm::half_pi<float>(), 0.0f, 0.0f };

			Sun = SceneData.CreateEntity("Sun");
			Sun.AddComponent<DirectionalLightComponent>().Intensity = 3.0f;
			Sun.GetComponent<TransformComponent>().Rotation = { -glm::quarter_pi<float>(), -glm::half_pi<float>(), 0.0f };

			Ground = SceneData.CreateEntity("Ground");
			Ground.GetComponent<TransformComponent>().Scale = { 40.0f, 1.0f, 40.0f };
			auto& ground = Ground.AddComponent<MeshRendererComponent>();
			ground.Primitive = PrimitiveType::Plane;
			ground.Material = { { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f, { 0, 0, 0 } };

			Cube = SceneData.CreateEntity("Cube");
			Cube.GetComponent<TransformComponent>().Translation = { 0.0f, cubeHeight, 0.0f };
			Cube.GetComponent<TransformComponent>().Scale = glm::vec3(cubeSize);
			auto& cube = Cube.AddComponent<MeshRendererComponent>();
			cube.Primitive = PrimitiveType::Cube;
			cube.Material = { { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f, { 0, 0, 0 } }; // same albedo as the ground
		}

		// Pixel for a ground point (the camera sees 11.5 world units across at ground level over 128 pixels).
		static glm::ivec2 Pixel(float x, float z) { return { int(64.0f + x * 11.1f), int(64.0f + z * 11.1f) }; }
	};

	float LuminanceAt(const ImageData& image, const glm::ivec2& p)
	{
		REQUIRE(p.x >= 0);
		REQUIRE(p.y >= 0);
		REQUIRE(uint32_t(p.x) < image.Width);
		REQUIRE(uint32_t(p.y) < image.Height);
		return Luminance(image.PixelAt(uint32_t(p.x), uint32_t(p.y)));
	}

	// Number of pixels along a row whose brightness lies between the shadow and the lit level (the penumbra).
	int PenumbraPixels(const ImageData& image, int row, int from, int to, float dark, float lit)
	{
		int count = 0;
		for (int x = from; x < to; x++)
		{
			const float l = Luminance(image.PixelAt(uint32_t(x), uint32_t(row)));
			if (l > dark + 0.2f * (lit - dark) && l < dark + 0.8f * (lit - dark))
				count++;
		}
		return count;
	}

}

TEST_CASE("Shadows fall on the side away from the light and only there")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeShadowRenderer(device);
	ShadowScene scene;
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData image = renderer->ReadOutput();

	const float lit = LuminanceAt(image, ShadowScene::Pixel(-4.0f, 0.0f));
	CHECK(lit > 120.0f);
	CHECK(LuminanceAt(image, ShadowScene::Pixel(2.0f, 0.0f)) < 25.0f);   // in the cube's shadow (x from 1 to 3)
	CHECK(LuminanceAt(image, ShadowScene::Pixel(2.0f, 0.8f)) < 25.0f);   // the shadow is as wide as the cube (z from -1 to 1)
	CHECK(LuminanceAt(image, ShadowScene::Pixel(2.0f, 1.6f)) > lit * 0.9f); // just outside that width: lit
	CHECK(LuminanceAt(image, ShadowScene::Pixel(2.0f, 3.5f)) > lit * 0.9f); // clearly beside it: lit
	CHECK(LuminanceAt(image, ShadowScene::Pixel(5.0f, 0.0f)) > lit * 0.9f); // beyond the shadow's tip: lit
	CHECK(LuminanceAt(image, ShadowScene::Pixel(-2.5f, 0.0f)) > lit * 0.9f); // on the light side of the cube: lit
}

TEST_CASE("Shadow softness widens the penumbra, and softness 0 gives a hard edge")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeShadowRenderer(device);
	ShadowScene scene(3.0f, 2.0f); // a cube floating above the ground so the penumbra has room to grow

	// Counts pixels that are neither fully lit nor fully shadowed over the whole frame. Cube and ground share an albedo,
	// so everything not on a shadow edge is either as bright as the ground or black.
	auto transitionPixels = [&](float softness)
	{
		renderer->GetSettings().ShadowSoftness = softness;
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		ImageData image = renderer->ReadOutput();
		const float lit = LuminanceAt(image, ShadowScene::Pixel(-4.0f, -4.0f));
		int count = 0;
		for (uint32_t y = 0; y < image.Height; y++)
			for (uint32_t x = 0; x < image.Width; x++)
			{
				const float l = Luminance(image.PixelAt(x, y));
				if (l > 0.15f * lit && l < 0.85f * lit)
					count++;
			}
		return count;
	};

	const int hard = transitionPixels(0.0f);
	const int soft = transitionPixels(0.2f);
	CHECK(soft > hard * 2);
	CHECK(hard < 400); // a hard shadow only has a thin edge
}

TEST_CASE("Contact hardening: the shadow edge is sharper where the caster touches the ground")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeShadowRenderer(device);
	renderer->GetSettings().ShadowSoftness = 0.15f;

	// Width (in pixels) of the transition across the shadow's +Z edge, measured along the shadow's center line.
	// With the sun at 45 degrees the shadow of a cube at height h is centered h units to the right of it.
	auto edgeWidth = [&](float height)
	{
		ShadowScene scene(height, 2.0f);
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		ImageData image = renderer->ReadOutput();
		const float lit = LuminanceAt(image, ShadowScene::Pixel(-4.0f, -4.0f));
		const int column = ShadowScene::Pixel(height, 0.0f).x;
		int count = 0;
		for (int y = ShadowScene::Pixel(0.0f, 0.0f).y; y < int(image.Height); y++)
		{
			const float l = Luminance(image.PixelAt(uint32_t(column), uint32_t(y)));
			if (l > 0.15f * lit && l < 0.85f * lit)
				count++;
		}
		return count;
	};

	const int touching = edgeWidth(1.0f);   // cube resting on the ground
	const int floating = edgeWidth(4.0f);   // cube well above it
	CHECK(floating > touching);
	CHECK(floating >= touching + 2);
}

TEST_CASE("Shadows can be turned off globally or per object; translucent objects do not cast")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeShadowRenderer(device);
	const glm::ivec2 shadowPixel = ShadowScene::Pixel(2.0f, 0.0f);

	ShadowScene normal;
	REQUIRE(renderer->Render(normal.SceneData, normal.Camera));
	const float shadowed = LuminanceAt(renderer->ReadOutput(), shadowPixel);
	CHECK(shadowed < 25.0f);

	renderer->GetSettings().EnableShadows = false;
	REQUIRE(renderer->Render(normal.SceneData, normal.Camera));
	CHECK(LuminanceAt(renderer->ReadOutput(), shadowPixel) > 120.0f);
	renderer->GetSettings().EnableShadows = true;

	ShadowScene noCast;
	noCast.Cube.GetComponent<MeshRendererComponent>().CastShadows = false;
	REQUIRE(renderer->Render(noCast.SceneData, noCast.Camera));
	CHECK(LuminanceAt(renderer->ReadOutput(), shadowPixel) > 120.0f);

	ShadowScene glass;
	glass.Cube.GetComponent<MeshRendererComponent>().Material.BaseColor.a = 0.4f;
	REQUIRE(renderer->Render(glass.SceneData, glass.Camera));
	CHECK(LuminanceAt(renderer->ReadOutput(), shadowPixel) > 120.0f);

	ShadowScene noLight;
	noLight.SceneData.DestroyEntity(noLight.Sun);
	REQUIRE(renderer->Render(noLight.SceneData, noLight.Camera)); // no light: nothing to shadow, must not crash
}

TEST_CASE("No shadow acne on surfaces lit at a grazing angle")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeShadowRenderer(device);
	ShadowScene scene(1.0f, 0.5f);
	scene.Cube.GetComponent<TransformComponent>().Translation = { 30.0f, 0.25f, 30.0f }; // a caster far from the area we inspect
	scene.Sun.GetComponent<TransformComponent>().Rotation = { -glm::radians(12.0f), -glm::half_pi<float>(), 0.0f }; // 12 degrees above the horizon
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	ImageData image = renderer->ReadOutput();

	float minimum = 1e9f, maximum = -1e9f;
	for (int y = 20; y < 108; y += 4)
		for (int x = 20; x < 108; x += 4)
		{
			const float l = Luminance(image.PixelAt(uint32_t(x), uint32_t(y)));
			minimum = std::min(minimum, l);
			maximum = std::max(maximum, l);
		}
	CHECK(maximum > 5.0f);               // the ground is actually lit
	CHECK(maximum - minimum < 6.0f);     // and evenly: no striping or speckle
}

TEST_CASE("Shadow map size is validated")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;
	CHECK(Renderer::Create(device, 64, 64, 100) == nullptr);
	CHECK(Renderer::Create(device, 64, 64, 16384) == nullptr);
	CHECK(Renderer::Create(device, 64, 64, 256) != nullptr);
}

// ---------------------------------------------------------------------------------------------
// Ambient occlusion
// ---------------------------------------------------------------------------------------------

namespace {

	// Reads the R8 occlusion buffer (1 = unoccluded) as floats.
	struct AoImage
	{
		ImageData Data;
		float At(uint32_t x, uint32_t y) const { return static_cast<float>(*Data.PixelAt(x, y)) / 255.0f; }
	};

	AoImage ReadAo(const Ref<RenderDevice>& device, const Ref<Renderer>& renderer)
	{
		return { device->ReadTexture(renderer->GetAmbientOcclusion()) };
	}

	Ref<Renderer> MakeAoRenderer(const Ref<RenderDevice>& device, uint32_t size = 128)
	{
		Ref<Renderer> renderer = Renderer::Create(device, size, size, 256);
		REQUIRE(renderer != nullptr);
		renderer->GetSettings().Ambient = glm::vec3(1.0f);
		renderer->GetSettings().EnableShadows = false;
		return renderer;
	}

	// A floor, optionally with a wall standing on it, seen from an oblique camera.
	struct AoScene
	{
		Scene SceneData;
		Entity Camera, Floor, Wall;

		explicit AoScene(bool withWall)
		{
			Camera = SceneData.CreateEntity("Camera");
			Camera.AddComponent<CameraComponent>();
			Camera.GetComponent<TransformComponent>().Translation = { 0.0f, 6.0f, 6.0f };
			Camera.GetComponent<TransformComponent>().Rotation = { -glm::radians(45.0f), 0.0f, 0.0f };

			Floor = SceneData.CreateEntity("Floor");
			Floor.GetComponent<TransformComponent>().Scale = { 30.0f, 1.0f, 30.0f };
			Floor.AddComponent<MeshRendererComponent>().Primitive = PrimitiveType::Plane;

			if (withWall)
			{
				Wall = SceneData.CreateEntity("Wall"); // a slab 8 wide, 4 tall, 0.2 thick, standing on the floor at z = -1
				Wall.GetComponent<TransformComponent>().Translation = { 0.0f, 2.0f, -1.0f };
				Wall.GetComponent<TransformComponent>().Scale = { 8.0f, 4.0f, 0.2f };
				Wall.AddComponent<MeshRendererComponent>().Primitive = PrimitiveType::Cube;
			}
		}
	};

}

namespace {

	// Row of the darkest pixel in a column (the crease where the wall meets the floor).
	uint32_t DarkestRow(const AoImage& ao, uint32_t column, uint32_t from, uint32_t to)
	{
		uint32_t best = from;
		for (uint32_t y = from; y < to; y++)
			if (ao.At(column, y) < ao.At(column, best))
				best = y;
		return best;
	}

	int CountBelow(const AoImage& ao, float threshold)
	{
		int count = 0;
		for (uint32_t y = 0; y < ao.Data.Height; y++)
			for (uint32_t x = 0; x < ao.Data.Width; x++)
				if (ao.At(x, y) < threshold)
					count++;
		return count;
	}

}

TEST_CASE("Ambient occlusion leaves open surfaces and isolated convex shapes unoccluded")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeAoRenderer(device);
	AoScene floorOnly(false);
	REQUIRE(renderer->Render(floorOnly.SceneData, floorOnly.Camera));
	AoImage ao = ReadAo(device, renderer);
	for (uint32_t y = 60; y < 126; y += 6)
		for (uint32_t x = 10; x < 120; x += 10)
			CHECK(ao.At(x, y) >= 0.97f);

	// A lone sphere floating in the air: no self occlusion, and the empty sky is exactly 1.
	AoScene air(false);
	air.SceneData.DestroyEntity(air.Floor);
	air.Camera.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };
	air.Camera.GetComponent<TransformComponent>().Rotation = { 0.0f, 0.0f, 0.0f };
	Entity sphere = air.SceneData.CreateEntity("Sphere");
	sphere.GetComponent<TransformComponent>().Scale = glm::vec3(3.0f);
	sphere.AddComponent<MeshRendererComponent>().Primitive = PrimitiveType::Sphere;
	REQUIRE(renderer->Render(air.SceneData, air.Camera));
	ao = ReadAo(device, renderer);
	CHECK(ao.At(2, 2) == doctest::Approx(1.0f));
	for (uint32_t y = 40; y < 90; y += 5)
		for (uint32_t x = 40; x < 90; x += 5)
			CHECK(ao.At(x, y) >= 0.96f);
}

TEST_CASE("Ambient occlusion darkens creases and fades with distance from them")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeAoRenderer(device);
	AoScene scene(true);
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	AoImage ao = ReadAo(device, renderer);

	const uint32_t column = 64;
	const uint32_t crease = DarkestRow(ao, column, 40, 90);
	CHECK(ao.At(column, crease) < 0.75f);   // clearly occluded in the corner

	// Moving away from the crease across the floor the occlusion fades out (never gets darker), reaching about 1.
	float previous = ao.At(column, crease + 2);
	for (uint32_t y = crease + 4; y < crease + 30; y += 2)
	{
		const float now = ao.At(column, y);
		CHECK(now >= previous - 0.04f);
		previous = now;
	}
	CHECK(ao.At(column, crease + 30) >= 0.97f);

	// The upper part of the wall, far from the floor, is open.
	CHECK(ao.At(column, 20) >= 0.97f);
}

TEST_CASE("Ambient occlusion grounds objects: contact darkening under a sphere")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeAoRenderer(device);
	AoScene scene(false);
	Entity ball = scene.SceneData.CreateEntity("Ball");
	ball.GetComponent<TransformComponent>().Translation = { 0.0f, 0.5f, 0.0f };
	ball.AddComponent<MeshRendererComponent>().Primitive = PrimitiveType::Sphere; // radius 0.5, resting on the floor
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	AoImage ao = ReadAo(device, renderer);

	float darkest = 1.0f;
	for (uint32_t y = 0; y < ao.Data.Height; y++)
		for (uint32_t x = 0; x < ao.Data.Width; x++)
			darkest = std::min(darkest, ao.At(x, y));
	CHECK(darkest < 0.85f);          // the ring where floor and sphere meet
	CHECK(ao.At(10, 110) >= 0.97f);  // far floor untouched
}

TEST_CASE("Ambient occlusion parameters: intensity, radius and the global switch")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeAoRenderer(device);
	AoScene scene(true);

	auto render = [&]
	{
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		return ReadAo(device, renderer);
	};
	auto darkestInColumn = [](const AoImage& ao)
	{
		float darkest = 1.0f;
		for (uint32_t y = 40; y < 90; y++)
			darkest = std::min(darkest, ao.At(64, y));
		return darkest;
	};

	const AoImage normal = render();
	const float baseline = darkestInColumn(normal);

	renderer->GetSettings().AOIntensity = 0.0f;
	CHECK(darkestInColumn(render()) >= 0.99f); // no intensity, no occlusion
	renderer->GetSettings().AOIntensity = 2.0f;
	CHECK(darkestInColumn(render()) < baseline - 0.05f);
	renderer->GetSettings().AOIntensity = 1.0f;

	renderer->GetSettings().AORadius = 0.2f;
	const int smallArea = CountBelow(render(), 0.9f);
	renderer->GetSettings().AORadius = 1.5f;
	const int largeArea = CountBelow(render(), 0.9f);
	CHECK(largeArea > smallArea * 2);
	renderer->GetSettings().AORadius = 0.75f;

	// Switched off: rendering equals the zero-intensity result and the buffer is not needed.
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	const ImageData withAo = renderer->ReadOutput();
	renderer->GetSettings().AOIntensity = 0.0f;
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	const ImageData zeroIntensity = renderer->ReadOutput();
	renderer->GetSettings().AOIntensity = 1.0f;
	renderer->GetSettings().EnableAmbientOcclusion = false;
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	const ImageData disabled = renderer->ReadOutput();
	CHECK(disabled.Pixels == zeroIntensity.Pixels);
	CHECK(withAo.Pixels != disabled.Pixels);
}

TEST_CASE("Ambient occlusion shades the ambient term only where occluded")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeAoRenderer(device);
	AoScene scene(true);

	auto hdr = [&](bool enabled)
	{
		renderer->GetSettings().EnableAmbientOcclusion = enabled;
		REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
		return device->ReadTexture(renderer->GetHdrTarget());
	};
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	const AoImage ao = ReadAo(device, renderer);
	const uint32_t crease = DarkestRow(ao, 64, 40, 90);

	const ImageData withAo = hdr(true);
	const ImageData withoutAo = hdr(false);
	const float occluded = HdrPixel(withAo, 64, crease).r;
	const float reference = HdrPixel(withoutAo, 64, crease).r;
	CHECK(occluded < reference * 0.85f);                                   // darker in the crease
	CHECK(HdrPixel(withAo, 64, 110).r == doctest::Approx(HdrPixel(withoutAo, 64, 110).r).epsilon(0.01)); // identical on the open floor
}

TEST_CASE("Ambient occlusion also darkens image-based lighting, including specular occlusion")
{
	auto device = GetTestRenderDevice();
	if (!device)
		return;

	Ref<Renderer> renderer = MakeAoRenderer(device);
	REQUIRE(renderer->SetEnvironment(BuildEnvironment(UniformSky(1.0f))));
	renderer->GetSettings().ShowBackground = false;
	AoScene scene(true);
	scene.Floor.GetComponent<MeshRendererComponent>().Material = { { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.5f, { 0, 0, 0 } };

	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	const AoImage ao = ReadAo(device, renderer);
	const uint32_t crease = DarkestRow(ao, 64, 40, 90);
	const float lit = HdrPixel(device->ReadTexture(renderer->GetHdrTarget()), 64, crease).r;

	renderer->GetSettings().EnableAmbientOcclusion = false;
	REQUIRE(renderer->Render(scene.SceneData, scene.Camera));
	const float reference = HdrPixel(device->ReadTexture(renderer->GetHdrTarget()), 64, crease).r;
	CHECK(lit < reference * 0.9f);
}
