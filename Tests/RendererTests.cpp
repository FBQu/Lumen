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
