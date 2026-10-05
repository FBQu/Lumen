#include <doctest/doctest.h>

#include "Lumen/Core/Log.h"
#include "Lumen/Physics/PhysicsWorld.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

using namespace Lumen;

namespace {

	struct ErrorCounter
	{
		static inline int Errors = 0;
		static void Sink(LogLevel level, std::string_view) { if (level == LogLevel::Error) Errors++; }
	};

	void Simulate(PhysicsWorld& world, float seconds)
	{
		const int frames = static_cast<int>(seconds / PhysicsWorld::s_FixedTimestep + 0.5f);
		for (int i = 0; i < frames; i++)
			world.Step(PhysicsWorld::s_FixedTimestep);
	}

	Entity CreateGround(Scene& scene, float topY = 0.0f)
	{
		Entity ground = scene.CreateEntity("Ground");
		ground.GetComponent<TransformComponent>().Translation = { 0.0f, topY - 0.5f, 0.0f };
		ground.AddComponent<ColliderComponent>().HalfExtents = { 50.0f, 0.5f, 50.0f }; // no rigidbody => static
		return ground;
	}

	Entity CreateBall(Scene& scene, const glm::vec3& position, float radius = 0.5f)
	{
		Entity ball = scene.CreateEntity("Ball");
		ball.GetComponent<TransformComponent>().Translation = position;
		auto& collider = ball.AddComponent<ColliderComponent>();
		collider.Shape = ColliderShape::Sphere;
		collider.Radius = radius;
		ball.AddComponent<RigidbodyComponent>();
		return ball;
	}

}

TEST_CASE("PhysicsWorld creates bodies for colliders on start")
{
	Scene scene;
	CreateGround(scene);
	CreateBall(scene, { 0.0f, 5.0f, 0.0f });
	scene.CreateEntity("NoCollider");

	PhysicsWorld world;
	world.Start(scene);
	CHECK(world.IsRunning());
	CHECK(world.GetBodyCount() == 2);
	world.Stop();
	CHECK(world.GetBodyCount() == 0);
}

TEST_CASE("Dynamic bodies fall under gravity and write results back to the transform")
{
	Scene scene;
	Entity ball = CreateBall(scene, { 0.0f, 10.0f, 0.0f });

	PhysicsWorld world;
	world.Start(scene);
	Simulate(world, 1.0f);

	// y = 10 - 0.5 * g * t^2 (with a little damping)
	const float y = ball.GetComponent<TransformComponent>().Translation.y;
	CHECK(y < 6.0f);
	CHECK(y > 4.5f);
	CHECK(world.GetLinearVelocity(ball).y < -8.0f);
	world.Stop();
}

TEST_CASE("Bodies come to rest on a static ground")
{
	Scene scene;
	CreateGround(scene);
	Entity ball = CreateBall(scene, { 0.0f, 3.0f, 0.0f });

	PhysicsWorld world;
	world.Start(scene);
	Simulate(world, 4.0f);

	CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.05));
	CHECK(glm::length(world.GetLinearVelocity(ball)) < 0.1f);
	world.Stop();
}

TEST_CASE("Gravity can be changed")
{
	Scene scene;
	Entity ball = CreateBall(scene, { 0.0f, 0.0f, 0.0f });

	PhysicsWorld world;
	world.Start(scene);
	CHECK(world.GetGravity().y == doctest::Approx(-9.81f));
	world.SetGravity({ 0.0f, 0.0f, -10.0f });
	Simulate(world, 0.5f);

	CHECK(ball.GetComponent<TransformComponent>().Translation.z < -1.0f);
	CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.0f).epsilon(0.01));
	world.Stop();
}

TEST_CASE("Gravity scale zero floats and impulses and velocity move the body")
{
	Scene scene;
	Entity ball = CreateBall(scene, { 0.0f, 0.0f, 0.0f });
	ball.GetComponent<RigidbodyComponent>().GravityScale = 0.0f;
	ball.GetComponent<RigidbodyComponent>().LinearDamping = 0.0f;

	PhysicsWorld world;
	world.Start(scene);
	Simulate(world, 0.5f);
	CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.0f).epsilon(0.001));

	CHECK(world.AddImpulse(ball, { 2.0f, 0.0f, 0.0f })); // mass 1 => 2 m/s
	CHECK(world.GetLinearVelocity(ball).x == doctest::Approx(2.0f));

	CHECK(world.SetLinearVelocity(ball, { 0.0f, 4.0f, 0.0f }));
	Simulate(world, 1.0f);
	CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(4.0f).epsilon(0.02));

	// A force acts for a single simulation step: v = F * dt / m.
	world.SetLinearVelocity(ball, { 0.0f, 0.0f, 0.0f });
	CHECK(world.AddForce(ball, { 10.0f, 0.0f, 0.0f }));
	world.Step(PhysicsWorld::s_FixedTimestep);
	CHECK(world.GetLinearVelocity(ball).x == doctest::Approx(10.0f * PhysicsWorld::s_FixedTimestep).epsilon(0.02));

	// Applying it every step accelerates continuously: x = 0.5 * a * t^2 = 1.25 m after 0.5 s.
	world.SetLinearVelocity(ball, { 0.0f, 0.0f, 0.0f });
	const float startX = ball.GetComponent<TransformComponent>().Translation.x;
	for (int i = 0; i < 30; i++)
	{
		world.AddForce(ball, { 10.0f, 0.0f, 0.0f });
		world.Step(PhysicsWorld::s_FixedTimestep);
	}
	CHECK(ball.GetComponent<TransformComponent>().Translation.x - startX == doctest::Approx(1.25f).epsilon(0.08));
	world.Stop();
}

TEST_CASE("Forces and velocity changes are rejected for non-dynamic and invalid entities")
{
	Scene scene;
	Entity ground = CreateGround(scene);
	Entity nothing = scene.CreateEntity("Nothing");

	PhysicsWorld world;
	world.Start(scene);

	CHECK_FALSE(world.AddForce(ground, { 1.0f, 0.0f, 0.0f }));
	CHECK_FALSE(world.AddImpulse(ground, { 1.0f, 0.0f, 0.0f }));
	CHECK_FALSE(world.SetLinearVelocity(ground, { 1.0f, 0.0f, 0.0f }));
	CHECK_FALSE(world.AddForce(nothing, { 1.0f, 0.0f, 0.0f }));
	CHECK_FALSE(world.AddForce(Entity{}, { 1.0f, 0.0f, 0.0f }));
	CHECK(world.GetLinearVelocity(ground) == glm::vec3(0.0f));
	world.Stop();
}

TEST_CASE("Writing a transform teleports the body")
{
	Scene scene;
	Entity ball = CreateBall(scene, { 0.0f, 0.0f, 0.0f });
	ball.GetComponent<RigidbodyComponent>().GravityScale = 0.0f;

	PhysicsWorld world;
	world.Start(scene);
	Simulate(world, 0.1f);

	ball.GetComponent<TransformComponent>().Translation = { 10.0f, 20.0f, 30.0f };
	world.Step(PhysicsWorld::s_FixedTimestep);

	const glm::vec3 position = ball.GetComponent<TransformComponent>().Translation;
	CHECK(position.x == doctest::Approx(10.0f).epsilon(0.01));
	CHECK(position.y == doctest::Approx(20.0f).epsilon(0.01));
	CHECK(position.z == doctest::Approx(30.0f).epsilon(0.01));
	world.Stop();
}

TEST_CASE("Kinematic bodies follow their transform and push dynamic bodies")
{
	Scene scene;
	Entity platform = scene.CreateEntity("Platform");
	platform.AddComponent<ColliderComponent>().HalfExtents = { 5.0f, 0.5f, 5.0f };
	platform.AddComponent<RigidbodyComponent>().Type = BodyType::Kinematic;
	Entity ball = CreateBall(scene, { 0.0f, 1.0f, 0.0f });

	PhysicsWorld world;
	world.Start(scene);
	Simulate(world, 1.0f); // ball settles on the platform

	for (int i = 0; i < 60; i++)
	{
		platform.GetComponent<TransformComponent>().Translation.y += 0.05f; // rise 3 m over 1 s
		world.Step(PhysicsWorld::s_FixedTimestep);
	}

	CHECK(platform.GetComponent<TransformComponent>().Translation.y == doctest::Approx(3.0f).epsilon(0.01));
	CHECK(ball.GetComponent<TransformComponent>().Translation.y > 3.5f);
	world.Stop();
}

TEST_CASE("Destroying an entity removes its body")
{
	Scene scene;
	Entity ball = CreateBall(scene, { 0.0f, 5.0f, 0.0f });

	PhysicsWorld world;
	world.Start(scene);
	REQUIRE(world.GetBodyCount() == 1);

	scene.DestroyEntity(ball);
	CHECK(world.GetBodyCount() == 0);
	world.Step(PhysicsWorld::s_FixedTimestep);
	world.Stop();
}

TEST_CASE("Entities created after start get bodies on the next step")
{
	Scene scene;
	PhysicsWorld world;
	world.Start(scene);
	CHECK(world.GetBodyCount() == 0);

	CreateBall(scene, { 0.0f, 5.0f, 0.0f });
	world.Step(PhysicsWorld::s_FixedTimestep);
	CHECK(world.GetBodyCount() == 1);
	world.Stop();
}

TEST_CASE("Fixed timestep accumulation and substep clamp")
{
	Scene scene;
	PhysicsWorld world;
	world.Start(scene);

	CHECK(world.Step(0.0f) == 0);
	CHECK(world.Step(-1.0f) == 0);
	CHECK(world.Step(PhysicsWorld::s_FixedTimestep * 0.5f) == 0);
	CHECK(world.Step(PhysicsWorld::s_FixedTimestep * 0.5f) == 1); // accumulated to one full step
	CHECK(world.Step(PhysicsWorld::s_FixedTimestep * 3.0f) == 3);
	CHECK(world.Step(10.0f) == PhysicsWorld::s_MaxSubsteps);       // long hitch is clamped
	CHECK(world.Step(PhysicsWorld::s_FixedTimestep * 0.5f) == 0);  // backlog was dropped
	world.Stop();
}

TEST_CASE("Raycast hits colliders and reports entity, point, normal and distance")
{
	Scene scene;
	Entity ground = CreateGround(scene);

	PhysicsWorld world;
	world.Start(scene);

	auto hit = world.Raycast({ 3.0f, 10.0f, 2.0f }, { 0.0f, -2.0f, 0.0f }, 100.0f);
	REQUIRE(hit.has_value());
	CHECK(hit->HitEntity == ground);
	CHECK(hit->Distance == doctest::Approx(10.0f).epsilon(0.001));
	CHECK(hit->Point.y == doctest::Approx(0.0f).epsilon(0.01));
	CHECK(hit->Normal.y == doctest::Approx(1.0f));

	CHECK_FALSE(world.Raycast({ 3.0f, 10.0f, 2.0f }, { 0.0f, -1.0f, 0.0f }, 5.0f).has_value());  // too short
	CHECK_FALSE(world.Raycast({ 3.0f, 10.0f, 2.0f }, { 0.0f, 1.0f, 0.0f }, 100.0f).has_value());  // wrong way
	CHECK_FALSE(world.Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 100.0f).has_value());  // zero direction
	CHECK_FALSE(world.Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 0.0f).has_value());   // zero range
	world.Stop();
}

TEST_CASE("Scale multiplies collider size and offsets shift the shape")
{
	Scene scene;
	Entity box = scene.CreateEntity("ScaledBox");
	box.GetComponent<TransformComponent>().Scale = { 4.0f, 1.0f, 1.0f };
	auto& collider = box.AddComponent<ColliderComponent>();
	collider.HalfExtents = { 0.5f, 0.5f, 0.5f };
	collider.Offset = { 0.0f, 3.0f, 0.0f };

	PhysicsWorld world;
	world.Start(scene);

	auto hit = world.Raycast({ 1.5f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 20.0f); // x=1.5 is inside half-width 2
	REQUIRE(hit.has_value());
	CHECK(hit->Point.y == doctest::Approx(3.5f).epsilon(0.01));
	CHECK_FALSE(world.Raycast({ 2.5f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 20.0f).has_value());
	world.Stop();
}

TEST_CASE("Capsule colliders are supported and fixed rotation keeps orientation")
{
	Scene scene;
	CreateGround(scene);
	Entity capsule = scene.CreateEntity("Capsule");
	capsule.GetComponent<TransformComponent>().Translation = { 0.0f, 3.0f, 0.0f };
	capsule.GetComponent<TransformComponent>().Rotation = { 0.1f, 0.0f, 0.0f }; // slightly tilted
	auto& collider = capsule.AddComponent<ColliderComponent>();
	collider.Shape = ColliderShape::Capsule;
	collider.Radius = 0.4f;
	collider.HalfHeight = 0.6f;
	capsule.AddComponent<RigidbodyComponent>().FixedRotation = true;

	PhysicsWorld world;
	world.Start(scene);
	Simulate(world, 3.0f);

	const auto& transform = capsule.GetComponent<TransformComponent>();
	CHECK(transform.Translation.y == doctest::Approx(1.0f).epsilon(0.05)); // half height + radius
	CHECK(transform.Rotation.x == doctest::Approx(0.1f).epsilon(0.01));
	world.Stop();
}

TEST_CASE("Invalid colliders and masses are rejected without bodies")
{
	ErrorCounter::Errors = 0;
	Log::SetSink(&ErrorCounter::Sink);

	Scene scene;
	Entity flat = scene.CreateEntity("Flat");
	flat.AddComponent<ColliderComponent>().HalfExtents = { 1.0f, 0.0f, 1.0f };
	Entity massless = CreateBall(scene, { 0.0f, 1.0f, 0.0f });
	massless.GetComponent<RigidbodyComponent>().Mass = 0.0f;
	Entity noRadius = CreateBall(scene, { 0.0f, 2.0f, 0.0f }, 0.0f);

	PhysicsWorld world;
	world.Start(scene);
	CHECK(world.GetBodyCount() == 0);
	CHECK(ErrorCounter::Errors == 3);
	world.Step(PhysicsWorld::s_FixedTimestep);
	world.Step(PhysicsWorld::s_FixedTimestep);
	CHECK(ErrorCounter::Errors == 3); // rejected entities are not retried (and re-logged) every step
	world.Stop();
	Log::SetSink(nullptr);
}

TEST_CASE("Multiple worlds can coexist and restart")
{
	Scene sceneA, sceneB;
	CreateBall(sceneA, { 0.0f, 1.0f, 0.0f });
	CreateBall(sceneB, { 0.0f, 1.0f, 0.0f });

	PhysicsWorld a;
	PhysicsWorld b;
	a.Start(sceneA);
	b.Start(sceneB);
	CHECK(a.GetBodyCount() == 1);
	CHECK(b.GetBodyCount() == 1);

	a.Stop();
	a.Start(sceneA);
	CHECK(a.GetBodyCount() == 1);
	a.Stop();
	b.Stop();
}
