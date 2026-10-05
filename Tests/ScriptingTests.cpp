#include <doctest/doctest.h>

#include "Lumen/Core/Log.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"
#include "Lumen/Scripting/ScriptEngine.h"

#include <string>
#include <vector>

using namespace Lumen;

namespace {

	struct LogCapture
	{
		static inline std::vector<std::pair<LogLevel, std::string>> Messages;
		static void Sink(LogLevel level, std::string_view message) { Messages.emplace_back(level, std::string(message)); }

		LogCapture() { Messages.clear(); Log::SetSink(&LogCapture::Sink); }
		~LogCapture() { Log::SetSink(nullptr); }

		bool Contains(LogLevel level, const std::string& text) const
		{
			for (auto& [l, m] : Messages)
				if (l == level && m.find(text) != std::string::npos)
					return true;
			return false;
		}
	};

	Entity AddScript(Scene& scene, const std::string& name, const std::string& source)
	{
		Entity e = scene.CreateEntity(name);
		e.AddComponent<ScriptComponent>(source);
		return e;
	}

}

TEST_CASE("ScriptEngine lifecycle: OnCreate once, OnUpdate every frame, OnDestroy on Stop")
{
	LogCapture capture;
	Scene scene;
	Entity e = AddScript(scene, "Counter", R"(
		local s = {}
		function s.OnCreate(entity) Counter_created = (Counter_created or 0) + 1 end
		function s.OnUpdate(entity, dt) Counter_updates = (Counter_updates or 0) + 1 end
		function s.OnDestroy(entity) Log.Info('counter destroyed') end
		return s
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.016f);
	engine.Update(0.016f);
	engine.Update(0.016f);

	CHECK(engine.GetInstanceCount() == 1);
	CHECK(engine.Execute("assert(Counter_created == 1)"));
	CHECK(engine.Execute("assert(Counter_updates == 3)"));
	CHECK_FALSE(capture.Contains(LogLevel::Info, "counter destroyed"));

	engine.Stop();
	CHECK(capture.Contains(LogLevel::Info, "counter destroyed"));
	CHECK_FALSE(engine.IsRunning());
	CHECK(engine.GetInstanceCount() == 0);
}

TEST_CASE("Scripts receive delta time and can move their entity")
{
	Scene scene;
	Entity e = AddScript(scene, "Mover", R"(
		return { OnUpdate = function(entity, dt)
			local t = entity.Transform
			t.Translation = t.Translation + vec3.new(1, 0, 0) * dt
		end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.5f);
	engine.Update(0.5f);

	CHECK(e.GetComponent<TransformComponent>().Translation.x == doctest::Approx(1.0f));
	engine.Stop();
}

TEST_CASE("Vec3 fields are assignable in place")
{
	Scene scene;
	Entity e = AddScript(scene, "Setter", R"(
		return { OnCreate = function(entity)
			entity.Transform.Translation.y = 7
			entity.Transform.Scale = vec3.new(2, 2, 2)
			entity.Name = "Renamed"
		end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);

	CHECK(e.GetComponent<TransformComponent>().Translation.y == doctest::Approx(7.0f));
	CHECK(e.GetComponent<TransformComponent>().Scale.x == doctest::Approx(2.0f));
	CHECK(e.GetName() == "Renamed");
	engine.Stop();
}

TEST_CASE("Scripts can create, find and destroy entities")
{
	Scene scene;
	AddScript(scene, "Spawner", R"(
		return { OnCreate = function(entity)
			local a = Scene.CreateEntity("Spawned")
			a.Transform.Translation = vec3.new(1, 2, 3)
			local found = Scene.FindEntityByName("Spawned")
			assert(found == a)
			assert(Scene.FindEntityByID(a.ID) == a)
			assert(Scene.FindEntityByName("nope") == nil)
			Scene.DestroyEntity(Scene.CreateEntity("Doomed"))
			assert(Scene.FindEntityByName("Doomed") == nil)
		end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);

	Entity spawned = scene.FindEntityByName("Spawned");
	REQUIRE(spawned.IsValid());
	CHECK(spawned.GetComponent<TransformComponent>().Translation.z == doctest::Approx(3.0f));
	CHECK(scene.GetEntityCount() == 2); // Spawner + Spawned
	engine.Stop();
}

TEST_CASE("Destroying an entity runs OnDestroy and removes the instance")
{
	Scene scene;
	Entity e = AddScript(scene, "Victim", R"(
		return { OnDestroy = function(entity) Scene.CreateEntity("Remains of " .. entity.Name) end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);
	REQUIRE(engine.GetInstanceCount() == 1);

	scene.DestroyEntity(e);

	CHECK(engine.GetInstanceCount() == 0);
	CHECK(scene.FindEntityByName("Remains of Victim").IsValid());
	engine.Update(0.0f); // must not touch the dead entity
	engine.Stop();
}

TEST_CASE("A script destroying itself during update is safe")
{
	Scene scene;
	AddScript(scene, "Suicidal", R"(
		return { OnUpdate = function(entity) Scene.DestroyEntity(entity) end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);

	CHECK(scene.GetEntityCount() == 0);
	CHECK(engine.GetInstanceCount() == 0);
	engine.Stop();
}

TEST_CASE("Runtime errors are logged once and fault only the offending script")
{
	LogCapture capture;
	Scene scene;
	AddScript(scene, "Broken", "return { OnUpdate = function() error('boom') end }");
	Entity healthy = AddScript(scene, "Healthy", R"(
		return { OnUpdate = function(entity, dt) entity.Transform.Translation.x = entity.Transform.Translation.x + 1 end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);
	engine.Update(0.0f);

	CHECK(capture.Contains(LogLevel::Error, "boom"));
	size_t errors = 0;
	for (auto& [level, message] : LogCapture::Messages)
		if (level == LogLevel::Error)
			errors++;
	CHECK(errors == 1);
	CHECK(healthy.GetComponent<TransformComponent>().Translation.x == doctest::Approx(2.0f));
	engine.Stop();
}

TEST_CASE("Invalid scripts are rejected without crashing")
{
	LogCapture capture;
	Scene scene;
	AddScript(scene, "SyntaxError", "this is not lua");
	AddScript(scene, "NoTable", "return 5");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);
	engine.Update(0.0f);

	CHECK(capture.Contains(LogLevel::Error, "SyntaxError"));
	CHECK(capture.Contains(LogLevel::Error, "must return a table"));
	engine.Stop();
}

TEST_CASE("Script sandbox exposes no io, os, package or debug libraries")
{
	Scene scene;
	ScriptEngine engine;
	engine.Start(scene);

	CHECK(engine.Execute("assert(io == nil and os == nil and package == nil and debug == nil)"));
	CHECK(engine.Execute("assert(math.sqrt(16) == 4 and #string.rep('a', 3) == 3)"));
	CHECK_FALSE(engine.Execute("os.exit(1)"));
	engine.Stop();
}

TEST_CASE("Execute reports errors and requires a running engine")
{
	LogCapture capture;
	ScriptEngine engine;
	CHECK_FALSE(engine.Execute("x = 1"));

	Scene scene;
	engine.Start(scene);
	CHECK_FALSE(engine.Execute("error('bad')"));
	CHECK(capture.Contains(LogLevel::Error, "bad"));
	CHECK(engine.Execute("x = 1"));
	engine.Stop();
}

TEST_CASE("Restarting the engine yields a fresh Lua state")
{
	Scene scene;
	ScriptEngine engine;
	engine.Start(scene);
	CHECK(engine.Execute("Leaked = 42"));
	engine.Stop();

	engine.Start(scene);
	CHECK(engine.Execute("assert(Leaked == nil)"));
	engine.Stop();
}

// ---------------------------------------------------------------------------------------------
// Physics bindings
// ---------------------------------------------------------------------------------------------

#include "Lumen/Physics/PhysicsWorld.h"

namespace {

	void RunFrames(ScriptEngine& scripts, PhysicsWorld& physics, int frames)
	{
		for (int i = 0; i < frames; i++)
		{
			scripts.Update(PhysicsWorld::s_FixedTimestep);
			physics.Step(PhysicsWorld::s_FixedTimestep);
		}
	}

}

TEST_CASE("Scripts can build physics entities and they simulate")
{
	Scene scene;
	AddScript(scene, "Spawner", R"(
		return { OnCreate = function(self)
			local ground = Scene.CreateEntity("Ground")
			ground.Transform.Translation = vec3.new(0, -0.5, 0)
			ground:AddCollider({ Shape = "box", HalfExtents = vec3.new(50, 0.5, 50) })

			local ball = Scene.CreateEntity("Ball")
			ball.Transform.Translation = vec3.new(0, 4, 0)
			ball:AddCollider({ Shape = "sphere", Radius = 0.5 })
			ball:AddRigidbody({ Type = "dynamic", Mass = 2, Restitution = 0.0 })
			assert(ball:HasCollider() and ball:HasRigidbody() and not ground:HasRigidbody())
		end }
	)");

	PhysicsWorld physics;
	ScriptEngine scripts;
	physics.Start(scene);
	scripts.Start(scene, &physics);
	RunFrames(scripts, physics, 240);

	Entity ball = scene.FindEntityByName("Ball");
	REQUIRE(ball.IsValid());
	CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.05));
	CHECK(physics.GetBodyCount() == 2);

	scripts.Stop();
	physics.Stop();
}

TEST_CASE("Lua Physics API: forces, impulses, velocity, gravity and raycasts")
{
	Scene scene;
	Entity ground = scene.CreateEntity("Ground");
	ground.GetComponent<TransformComponent>().Translation = { 0.0f, -0.5f, 0.0f };
	ground.AddComponent<ColliderComponent>().HalfExtents = { 50.0f, 0.5f, 50.0f };
	Entity ball = scene.CreateEntity("Ball");
	ball.GetComponent<TransformComponent>().Translation = { 0.0f, 10.0f, 0.0f };
	ball.AddComponent<ColliderComponent>().Shape = ColliderShape::Sphere;
	ball.AddComponent<RigidbodyComponent>().GravityScale = 0.0f;

	PhysicsWorld physics;
	ScriptEngine scripts;
	physics.Start(scene);
	scripts.Start(scene, &physics);

	CHECK(scripts.Execute(R"(
		local g = Physics.GetGravity()
		assert(math.abs(g.y + 9.81) < 1e-4)
		Physics.SetGravity(vec3.new(0, -1, 0))
		assert(Physics.GetGravity().y == -1)
		Physics.SetGravity(vec3.new(0, -9.81, 0))

		local ball = Scene.FindEntityByName("Ball")
		assert(Physics.AddImpulse(ball, vec3.new(0, 0, 3)))
		assert(math.abs(Physics.GetVelocity(ball).z - 3) < 1e-4)
		assert(Physics.SetVelocity(ball, vec3.new(0, 0, 0)))
		assert(Physics.AddForce(ball, vec3.new(1, 0, 0)))
		assert(not Physics.AddForce(Scene.FindEntityByName("Ground"), vec3.new(1, 0, 0)))

		local hit = Physics.Raycast(vec3.new(5, 10, 0), vec3.new(0, -1, 0), 50)
		assert(hit ~= nil)
		assert(hit.Entity == Scene.FindEntityByName("Ground"))
		assert(math.abs(hit.Distance - 10) < 1e-3)
		assert(math.abs(hit.Normal.y - 1) < 1e-4)
		assert(Physics.Raycast(vec3.new(5, 10, 0), vec3.new(0, 1, 0), 50) == nil)
	)"));

	scripts.Stop();
	physics.Stop();
}

TEST_CASE("Invalid physics options raise Lua errors; Physics table needs a world")
{
	LogCapture capture;
	Scene scene;
	scene.CreateEntity("E");

	{
		PhysicsWorld physics;
		ScriptEngine scripts;
		physics.Start(scene);
		scripts.Start(scene, &physics);
		CHECK_FALSE(scripts.Execute("Scene.FindEntityByName('E'):AddRigidbody({ Type = 'bogus' })"));
		CHECK(capture.Contains(LogLevel::Error, "invalid body type"));
		CHECK_FALSE(scripts.Execute("Scene.FindEntityByName('E'):AddCollider({ Shape = 'torus' })"));
		CHECK(capture.Contains(LogLevel::Error, "invalid collider shape"));
		scripts.Stop();
		physics.Stop();
	}

	ScriptEngine scripts;
	scripts.Start(scene); // no physics world
	CHECK(scripts.Execute("assert(Physics == nil)"));
	CHECK(scripts.Execute("Scene.FindEntityByName('E'):AddCollider()")); // components still usable
	scripts.Stop();
}
