#include <doctest/doctest.h>

#include "GltfTestData.h"
#include "Lumen/Assets/AssetManager.h"
#include "Lumen/Scene/Scene.h"

#include <filesystem>
#include <fstream>

using namespace Lumen;
using namespace GltfTestData;

namespace {

	// Writes a glTF whose child node also carries the mesh, into a fresh temp directory.
	struct TempModel
	{
		std::filesystem::path Dir;
		std::string Path;

		TempModel(const std::string& name)
		{
			Dir = std::filesystem::temp_directory_path() / ("lumen_assets_" + name);
			std::filesystem::create_directories(Dir);
			std::string json = TriangleGltf();
			const std::string needle = R"({"name":"Child","scale":[2,2,2]})";
			const size_t at = json.find(needle);
			REQUIRE(at != std::string::npos);
			json.replace(at, needle.size(), R"({"name":"Child","mesh":0,"scale":[2,2,2]})");
			Path = (Dir / "model.gltf").string();
			std::ofstream(Path) << json;
		}
		~TempModel() { std::filesystem::remove_all(Dir); }
	};

}

TEST_CASE("MakeAssetID is stable, distinguishes its inputs and is never zero")
{
	CHECK(static_cast<uint64_t>(AssetManager::MakeAssetID("a.gltf", "mesh", 0)) == 9833905343251478983ull); // golden: ids must not change between versions
	CHECK(static_cast<uint64_t>(AssetManager::MakeAssetID("a.gltf", "model", 0)) == 4938304750714509135ull);

	const UUID base = AssetManager::MakeAssetID("a.gltf", "mesh", 0);
	CHECK(base == AssetManager::MakeAssetID("a.gltf", "mesh", 0));
	CHECK_FALSE(base == AssetManager::MakeAssetID("b.gltf", "mesh", 0));
	CHECK_FALSE(base == AssetManager::MakeAssetID("a.gltf", "texture", 0));
	CHECK_FALSE(base == AssetManager::MakeAssetID("a.gltf", "mesh", 1));
	CHECK(base.IsValid());
}

TEST_CASE("Runtime-created assets get distinct ids and can be looked up")
{
	AssetManager assets;
	const UUID mesh = assets.AddMesh({ "Quad", MeshGenerator::CreatePlane() });
	const UUID texture = assets.AddTexture({ "Tex", {} });
	MaterialAsset material;
	material.Name = "Mat";
	material.Metallic = 0.5f;
	const UUID materialID = assets.AddMaterial(material);

	CHECK(mesh.IsValid());
	CHECK(assets.GetMesh(mesh)->Name == "Quad");
	CHECK(assets.GetMesh(mesh)->Data.GetTriangleCount() == 2);
	CHECK(assets.GetTexture(texture)->Name == "Tex");
	CHECK(assets.GetMaterial(materialID)->Metallic == doctest::Approx(0.5f));
	CHECK(assets.AddMesh({ "Quad", MeshGenerator::CreatePlane() }) != mesh);
	CHECK(assets.GetMeshCount() == 2);

	CHECK(assets.GetMesh(UUID(12345)) == nullptr);
	CHECK(assets.GetTexture(UUID(12345)) == nullptr);
	CHECK(assets.GetMaterial(UUID(12345)) == nullptr);
	CHECK(assets.GetModel(UUID(12345)) == nullptr);
}

TEST_CASE("ImportGltf registers assets with deterministic ids and is idempotent")
{
	TempModel file("import");
	AssetManager assets;
	std::string error;
	auto model = assets.ImportGltf(file.Path, &error);
	REQUIRE_MESSAGE(model.has_value(), error);

	CHECK(assets.GetMeshCount() == 1);
	CHECK(assets.GetMaterialCount() == 1);
	CHECK(assets.GetTextureCount() == 0);
	const ModelAsset* asset = assets.GetModel(*model);
	REQUIRE(asset != nullptr);
	CHECK(asset->Name == "model.gltf");
	REQUIRE(asset->Nodes.size() == 2);
	REQUIRE(asset->Nodes[0].Primitives.size() == 1);

	const ModelNode::Primitive& primitive = asset->Nodes[0].Primitives[0];
	REQUIRE(assets.GetMesh(primitive.Mesh) != nullptr);
	REQUIRE(assets.GetMaterial(primitive.Material) != nullptr);
	CHECK(assets.GetMaterial(primitive.Material)->Name == "Red");
	CHECK(assets.GetMaterial(primitive.Material)->Metallic == doctest::Approx(0.25f));
	CHECK(assets.GetMaterial(primitive.Material)->Alpha == AlphaMode::Mask);
	CHECK(primitive.Mesh == asset->Nodes[1].Primitives[0].Mesh); // both nodes share the mesh

	// Importing again changes nothing.
	CHECK(assets.ImportGltf(file.Path) == model);
	CHECK(assets.GetMeshCount() == 1);

	// A second manager derives the very same ids from the same path.
	AssetManager other;
	auto otherModel = other.ImportGltf(file.Path);
	REQUIRE(otherModel.has_value());
	CHECK(*otherModel == *model);
	CHECK(other.GetModel(*otherModel)->Nodes[0].Primitives[0].Mesh == primitive.Mesh);
	CHECK(other.GetModel(*otherModel)->Nodes[0].Primitives[0].Material == primitive.Material);
}

TEST_CASE("ImportGltf reports failures and leaves the manager untouched")
{
	AssetManager assets;
	std::string error;
	CHECK_FALSE(assets.ImportGltf("/definitely/not/here.gltf", &error).has_value());
	CHECK(error.find("could not read") != std::string::npos);
	CHECK(assets.GetMeshCount() == 0);
	CHECK(assets.GetMaterialCount() == 0);
}

TEST_CASE("Instantiate composes node transforms and wires asset references")
{
	TempModel file("instantiate");
	AssetManager assets;
	UUID model = *assets.ImportGltf(file.Path);

	Scene scene;
	std::vector<Entity> entities = assets.Instantiate(scene, model);
	REQUIRE(entities.size() == 2);
	CHECK(scene.GetEntityCount() == 2);

	Entity root = entities[0];
	Entity child = entities[1];
	CHECK(root.GetName() == "Root");
	CHECK(child.GetName() == "Child");

	const TransformComponent& rootTransform = root.GetComponent<TransformComponent>();
	CHECK(rootTransform.Translation == glm::vec3(1, 2, 3));
	CHECK(rootTransform.Scale == glm::vec3(1, 1, 1));

	// The child sits at its parent's position (its own local translation is zero) and carries its own scale of 2.
	const TransformComponent& childTransform = child.GetComponent<TransformComponent>();
	CHECK(childTransform.Translation.x == doctest::Approx(1.0f));
	CHECK(childTransform.Translation.y == doctest::Approx(2.0f));
	CHECK(childTransform.Translation.z == doctest::Approx(3.0f));
	CHECK(childTransform.Scale.x == doctest::Approx(2.0f));

	const ModelNode::Primitive& primitive = assets.GetModel(model)->Nodes[0].Primitives[0];
	CHECK(root.GetComponent<MeshRendererComponent>().MeshAsset == primitive.Mesh);
	CHECK(root.GetComponent<MeshRendererComponent>().MaterialAsset == primitive.Material);
	CHECK(child.GetComponent<MeshRendererComponent>().MeshAsset == primitive.Mesh);

	// A root transform moves the whole model.
	Scene shifted;
	assets.Instantiate(shifted, model, glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)));
	CHECK(shifted.FindEntityByName("Root").GetComponent<TransformComponent>().Translation.x == doctest::Approx(11.0f));
	CHECK(shifted.FindEntityByName("Child").GetComponent<TransformComponent>().Translation.x == doctest::Approx(11.0f));

	// Instantiating twice makes independent copies.
	assets.Instantiate(scene, model);
	CHECK(scene.GetEntityCount() == 4);
}

TEST_CASE("Instantiate with an unknown model creates nothing")
{
	AssetManager assets;
	Scene scene;
	CHECK(assets.Instantiate(scene, UUID(777)).empty());
	CHECK(scene.GetEntityCount() == 0);
}
