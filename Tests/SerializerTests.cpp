#include <doctest/doctest.h>

#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"
#include "Lumen/Scene/SceneSerializer.h"

#include <string>

using namespace Lumen;

namespace {

	std::string Wrap(const std::string& entities, const std::string& version = "1")
	{
		return "{\"version\":" + version + ",\"entities\":[" + entities + "]}";
	}

	bool Fails(const std::string& json, const std::string& expectedFragment)
	{
		Scene scene;
		std::string error;
		const bool ok = SceneSerializer::Deserialize(scene, json, &error);
		const bool matches = error.find(expectedFragment) != std::string::npos;
		if (ok || !matches || scene.GetEntityCount() != 0)
			MESSAGE("expected failure containing '" << expectedFragment << "', got ok=" << ok << " error='" << error << "'");
		return !ok && matches && scene.GetEntityCount() == 0;
	}

}

TEST_CASE("Scene round-trips through JSON with all components")
{
	Scene original;
	Entity a = original.CreateEntityWithUUID(UUID(18446744073709551615ull), "Ball"); // max uint64 survives
	a.GetComponent<TransformComponent>().Translation = { 1.5f, -2.0f, 3.25f };
	a.GetComponent<TransformComponent>().Rotation = { 0.1f, 0.2f, 0.3f };
	a.GetComponent<TransformComponent>().Scale = { 2.0f, 2.0f, 2.0f };
	auto& rb = a.AddComponent<RigidbodyComponent>();
	rb.Type = BodyType::Kinematic;
	rb.Mass = 3.0f;
	rb.FixedRotation = true;
	auto& collider = a.AddComponent<ColliderComponent>();
	collider.Shape = ColliderShape::Capsule;
	collider.Radius = 0.25f;
	collider.HalfHeight = 0.75f;
	collider.Offset = { 0.0f, 1.0f, 0.0f };
	a.AddComponent<ScriptComponent>("return { OnUpdate = function() end } -- \"quoted\"\nline2");
	Entity b = original.CreateEntity("Plain");

	const std::string json = SceneSerializer::Serialize(original);

	Scene loaded;
	std::string error;
	REQUIRE_MESSAGE(SceneSerializer::Deserialize(loaded, json, &error), error);
	CHECK(loaded.GetEntityCount() == 2);

	Entity a2 = loaded.FindEntityByUUID(UUID(18446744073709551615ull));
	REQUIRE(a2.IsValid());
	CHECK(a2.GetName() == "Ball");
	CHECK(a2.GetComponent<TransformComponent>().Translation == glm::vec3(1.5f, -2.0f, 3.25f));
	CHECK(a2.GetComponent<TransformComponent>().Rotation.z == doctest::Approx(0.3f));
	CHECK(a2.GetComponent<TransformComponent>().Scale == glm::vec3(2.0f));
	CHECK(a2.GetComponent<RigidbodyComponent>().Type == BodyType::Kinematic);
	CHECK(a2.GetComponent<RigidbodyComponent>().Mass == doctest::Approx(3.0f));
	CHECK(a2.GetComponent<RigidbodyComponent>().FixedRotation);
	CHECK(a2.GetComponent<ColliderComponent>().Shape == ColliderShape::Capsule);
	CHECK(a2.GetComponent<ColliderComponent>().HalfHeight == doctest::Approx(0.75f));
	CHECK(a2.GetComponent<ColliderComponent>().Offset.y == doctest::Approx(1.0f));
	CHECK(a2.GetComponent<ScriptComponent>().Source == a.GetComponent<ScriptComponent>().Source);

	Entity b2 = loaded.FindEntityByUUID(b.GetUUID());
	REQUIRE(b2.IsValid());
	CHECK_FALSE(b2.HasComponent<RigidbodyComponent>());
	CHECK_FALSE(b2.HasComponent<ColliderComponent>());
	CHECK_FALSE(b2.HasComponent<ScriptComponent>());

	CHECK(SceneSerializer::Serialize(loaded).size() == json.size()); // stable format
}

TEST_CASE("Empty scene and minimal entity with defaults")
{
	Scene scene;
	CHECK(SceneSerializer::Deserialize(scene, Wrap("")));
	CHECK(scene.GetEntityCount() == 0);

	CHECK(SceneSerializer::Deserialize(scene, Wrap(R"({"id":"7"})")));
	Entity e = scene.FindEntityByUUID(UUID(7));
	REQUIRE(e.IsValid());
	CHECK(e.GetName() == "Entity");
	CHECK(e.GetComponent<TransformComponent>().Scale == glm::vec3(1.0f));
}

TEST_CASE("Deserialize rejects malformed input and leaves the scene untouched")
{
	CHECK(Fails("not json", "invalid JSON"));
	CHECK(Fails("[]", "root must be an object"));
	CHECK(Fails("{}", "version"));
	CHECK(Fails(Wrap("", "2"), "unsupported scene version 2"));
	CHECK(Fails(R"({"version":1})", "'entities'"));
	CHECK(Fails(Wrap("5"), "must be an object"));
	CHECK(Fails(Wrap("{}"), "'id'"));
	CHECK(Fails(Wrap(R"({"id":7})"), "'id'"));
	CHECK(Fails(Wrap(R"({"id":"0"})"), "non-zero"));
	CHECK(Fails(Wrap(R"({"id":"abc"})"), "non-zero"));
	CHECK(Fails(Wrap(R"({"id":"-3"})"), "non-zero"));
	CHECK(Fails(Wrap(R"({"id":"99999999999999999999"})"), "non-zero"));
	CHECK(Fails(Wrap(R"({"id":"1","name":5})"), "'name' must be a string"));
	CHECK(Fails(Wrap(R"({"id":"1","transform":{"translation":[1,2]}})"), "3 numbers"));
	CHECK(Fails(Wrap(R"({"id":"1","transform":{"scale":["a","b","c"]}})"), "3 numbers"));
	CHECK(Fails(Wrap(R"({"id":"1","transform":5})"), "'transform'"));
	CHECK(Fails(Wrap(R"({"id":"1","rigidbody":{"type":"floaty"}})"), "unknown body type"));
	CHECK(Fails(Wrap(R"({"id":"1","rigidbody":{"mass":"heavy"}})"), "'mass' must be a number"));
	CHECK(Fails(Wrap(R"({"id":"1","rigidbody":{"fixedRotation":1}})"), "'fixedRotation' must be a boolean"));
	CHECK(Fails(Wrap(R"({"id":"1","collider":{"shape":"torus"}})"), "unknown collider shape"));
	CHECK(Fails(Wrap(R"({"id":"1","script":{"source":5}})"), "'source' must be a string"));
	CHECK(Fails(Wrap(R"({"id":"1"},{"id":"1"})"), "duplicate entity id 1"));
	// A valid entity before a bad one must not leak into the scene.
	CHECK(Fails(Wrap(R"({"id":"1"},{"id":"2","rigidbody":{"type":"x"}})"), "unknown body type"));
}

TEST_CASE("Deserialize refuses ids that already exist in the target scene")
{
	Scene scene;
	Entity existing = scene.CreateEntityWithUUID(UUID(5), "Existing");

	std::string error;
	CHECK_FALSE(SceneSerializer::Deserialize(scene, Wrap(R"({"id":"6"},{"id":"5"})"), &error));
	CHECK(error.find("already exists") != std::string::npos);
	CHECK(scene.GetEntityCount() == 1);
	CHECK(existing.GetName() == "Existing");

	CHECK(SceneSerializer::Deserialize(scene, Wrap(R"({"id":"8"})")));
	CHECK(scene.GetEntityCount() == 2);
}

TEST_CASE("Deserialize works without an error out-parameter")
{
	Scene scene;
	CHECK_FALSE(SceneSerializer::Deserialize(scene, "{"));
	CHECK(SceneSerializer::Deserialize(scene, Wrap(R"({"id":"3"})")));
}
