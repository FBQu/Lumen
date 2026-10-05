#include <doctest/doctest.h>

#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <glm/gtc/epsilon.hpp>

using namespace Lumen;

TEST_CASE("Scene creates entities with default components")
{
	Scene scene;
	Entity e = scene.CreateEntity("Cube");

	CHECK(e.IsValid());
	CHECK(e.GetName() == "Cube");
	CHECK(e.GetUUID().IsValid());
	CHECK(e.HasComponent<TransformComponent>());
	CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("Empty name falls back to a default")
{
	Scene scene;
	CHECK(scene.CreateEntity("").GetName() == "Entity");
}

TEST_CASE("Entities can be found by UUID and by name")
{
	Scene scene;
	Entity a = scene.CreateEntity("A");
	Entity b = scene.CreateEntityWithUUID(UUID(1234), "B");

	CHECK(scene.FindEntityByUUID(a.GetUUID()) == a);
	CHECK(scene.FindEntityByUUID(UUID(1234)) == b);
	CHECK(scene.FindEntityByName("B") == b);
	CHECK_FALSE(scene.FindEntityByName("missing").IsValid());
	CHECK_FALSE(scene.FindEntityByUUID(UUID(999)).IsValid());
}

TEST_CASE("Destroying an entity invalidates handles and lookups")
{
	Scene scene;
	Entity e = scene.CreateEntity("Temp");
	UUID id = e.GetUUID();

	scene.DestroyEntity(e);

	CHECK_FALSE(e.IsValid());
	CHECK_FALSE(scene.FindEntityByUUID(id).IsValid());
	CHECK(scene.GetEntityCount() == 0);

	scene.DestroyEntity(e); // double destroy is a safe no-op
	scene.DestroyEntity(Entity{});
	CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("Destroying an entity from another scene is ignored")
{
	Scene a, b;
	Entity e = a.CreateEntity("InA");
	b.DestroyEntity(e);
	CHECK(e.IsValid());
	CHECK(a.GetEntityCount() == 1);
}

TEST_CASE("Component add, replace and remove")
{
	struct Health { int Value; };

	Scene scene;
	Entity e = scene.CreateEntity();

	CHECK_FALSE(e.HasComponent<Health>());
	e.AddComponent<Health>(10);
	CHECK(e.GetComponent<Health>().Value == 10);

	e.AddOrReplaceComponent<Health>(25);
	CHECK(e.GetComponent<Health>().Value == 25);

	e.RemoveComponent<Health>();
	CHECK_FALSE(e.HasComponent<Health>());
}

TEST_CASE("Transform matrix composes translation, rotation and scale")
{
	TransformComponent t;
	t.Translation = { 1.0f, 2.0f, 3.0f };
	t.Scale = { 2.0f, 2.0f, 2.0f };

	glm::vec4 p = t.GetTransform() * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	CHECK(glm::all(glm::epsilonEqual(glm::vec3(p), glm::vec3(3.0f, 2.0f, 3.0f), 1e-5f)));

	t.Rotation = { 0.0f, glm::half_pi<float>(), 0.0f }; // 90 degrees around Y
	p = t.GetTransform() * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	CHECK(glm::all(glm::epsilonEqual(glm::vec3(p), glm::vec3(1.0f, 2.0f, 1.0f), 1e-5f)));
}

TEST_CASE("Clear destroys every entity and frees their ids")
{
	Scene scene;
	Entity a = scene.CreateEntityWithUUID(UUID(11), "A");
	scene.CreateEntity("B");
	scene.Clear();

	CHECK(scene.GetEntityCount() == 0);
	CHECK_FALSE(a.IsValid());
	CHECK_FALSE(scene.FindEntityByUUID(UUID(11)).IsValid());
	CHECK(scene.CreateEntityWithUUID(UUID(11), "Again").IsValid()); // id can be reused
	scene.Clear(); // clearing an already-empty scene is fine
	scene.Clear();
	CHECK(scene.GetEntityCount() == 0);
}
