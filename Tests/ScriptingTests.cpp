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

// ---------------------------------------------------------------------------------------------
// Prefab spawning, render component bindings, instruction budget
// ---------------------------------------------------------------------------------------------

#include "Lumen/Scene/SceneSerializer.h"

TEST_CASE("Scripts can spawn prefabs with overrides")
{
	LogCapture capture;
	Scene scene;
	Entity template_ = scene.CreateEntity("Pellet");
	template_.AddComponent<MeshRendererComponent>().Primitive = PrimitiveType::Sphere;
	REQUIRE(SceneSerializer::CreatePrefab(scene, "Pellet", template_));
	scene.DestroyEntity(template_);

	AddScript(scene, "Gun", R"(
		return { OnCreate = function(self)
			assert(Scene.HasPrefab("Pellet") and not Scene.HasPrefab("Nope"))
			local a = Scene.Spawn("Pellet")
			local b = Scene.Spawn("Pellet", { Name = "Pellet B", Translation = vec3.new(1, 2, 3), Scale = vec3.new(0.5, 0.5, 0.5) })
			assert(a ~= b and b.Name == "Pellet B" and a.Name == "Pellet")
			assert(b.Transform.Translation.y == 2)
			assert(b:HasMeshRenderer() and b.MeshRenderer.Primitive == "sphere")
			assert(#Scene.GetAllEntities() == 3) -- gun + two pellets
		end }
	)");

	ScriptEngine engine;
	engine.Start(scene);
	engine.Update(0.0f);
	CHECK(scene.GetEntityCount() == 3);
	CHECK(scene.FindEntityByName("Pellet B").GetComponent<TransformComponent>().Scale.x == doctest::Approx(0.5f));

	CHECK_FALSE(engine.Execute("Scene.Spawn('missing')"));
	CHECK(capture.Contains(LogLevel::Error, "does not exist"));
	CHECK_FALSE(engine.Execute("Scene.Spawn('Pellet', { Translation = 5 })")); // wrong type
	CHECK(scene.GetEntityCount() == 3);
	engine.Stop();
}

TEST_CASE("Scripts can read and write render components and add or remove components")
{
	Scene scene;
	scene.CreateEntity("Target");
	ScriptEngine engine;
	engine.Start(scene);

	std::string output, error;
	REQUIRE_MESSAGE(engine.Execute(R"(
		local e = Scene.FindEntityByName("Target")
		assert(not e:HasMeshRenderer() and not e:HasCamera() and not e:HasDirectionalLight())

		e:AddMeshRenderer({ Primitive = "plane", BaseColor = vec4.new(1, 0, 0, 0.5), Metallic = 0.25, Roughness = 0.75,
		                    Emissive = vec3.new(0, 1, 0), CastShadows = false })
		local m = e.MeshRenderer
		assert(m.Primitive == "plane" and m.Metallic == 0.25 and m.Roughness == 0.75 and m.CastShadows == false)
		assert(m.BaseColor.x == 1 and m.BaseColor.w == 0.5 and m.Emissive.y == 1)
		m.Primitive = "cube"
		m.BaseColor.y = 0.5          -- in-place edit of a nested vec4
		m.Emissive = vec3.new(2, 2, 2)
		m.Roughness = 0.1
		assert(e.MeshRenderer.Primitive == "cube" and e.MeshRenderer.BaseColor.y == 0.5 and e.MeshRenderer.Emissive.x == 2)

		e:AddCamera({ FovDegrees = 45, Near = 0.5, Far = 99 })
		assert(math.abs(e.Camera.FovDegrees - 45) < 1e-4 and e.Camera.Near == 0.5 and e.Camera.Far == 99)
		e.Camera.FovDegrees = 90
		assert(math.abs(e.Camera.FovDegrees - 90) < 1e-4)

		e:AddDirectionalLight({ Color = vec3.new(1, 0.5, 0.25), Intensity = 7 })
		assert(e.DirectionalLight.Intensity == 7 and e.DirectionalLight.Color.y == 0.5)
		e.DirectionalLight.Intensity = 2

		e:SetScript("return {}")
		e:RemoveComponent("MeshRenderer")
		e:RemoveComponent("MeshRenderer") -- removing a missing component is a no-op
		assert(not e:HasMeshRenderer() and e:HasCamera() and e:HasDirectionalLight())
		return "ok"
	)", &output, &error), error);
	CHECK(output == "ok");

	Entity target = scene.FindEntityByName("Target");
	CHECK_FALSE(target.HasComponent<MeshRendererComponent>());
	CHECK(target.GetComponent<DirectionalLightComponent>().Intensity == doctest::Approx(2.0f));
	CHECK(target.HasComponent<ScriptComponent>());
	engine.Stop();
}

TEST_CASE("Component accessors report mistakes as Lua errors instead of crashing")
{
	LogCapture capture;
	Scene scene;
	scene.CreateEntity("Bare");
	ScriptEngine engine;
	engine.Start(scene);

	CHECK_FALSE(engine.Execute("local e = Scene.FindEntityByName('Bare'); return e.MeshRenderer.Metallic"));
	CHECK(capture.Contains(LogLevel::Error, "no MeshRenderer component"));
	CHECK_FALSE(engine.Execute("Scene.FindEntityByName('Bare').Camera.Near = 1"));
	CHECK_FALSE(engine.Execute("Scene.FindEntityByName('Bare'):AddMeshRenderer({ Primitive = 'cone' })"));
	CHECK(capture.Contains(LogLevel::Error, "invalid primitive"));
	CHECK_FALSE(engine.Execute("Scene.FindEntityByName('Bare'):AddCamera({ FovDegrees = 400 })"));
	CHECK_FALSE(engine.Execute("Scene.FindEntityByName('Bare'):AddCamera({ Near = 5, Far = 1 })"));
	CHECK_FALSE(engine.Execute("Scene.FindEntityByName('Bare'):AddDirectionalLight({ Intensity = -1 })"));
	CHECK_FALSE(engine.Execute("Scene.FindEntityByName('Bare'):RemoveComponent('Transform')"));
	CHECK(capture.Contains(LogLevel::Error, "cannot remove component"));

	// A destroyed entity handle fails cleanly too.
	CHECK_FALSE(engine.Execute("local e = Scene.CreateEntity('Temp'); Scene.DestroyEntity(e); return e.MeshRenderer"));
	CHECK(capture.Contains(LogLevel::Error, "no longer exists"));
	CHECK(scene.FindEntityByName("Bare").IsValid()); // nothing was added by the failed calls
	CHECK_FALSE(scene.FindEntityByName("Bare").HasComponent<CameraComponent>());
	engine.Stop();
}

TEST_CASE("Runaway scripts are stopped by the instruction limit")
{
	LogCapture capture;
	Scene scene;
	AddScript(scene, "Spinner", "return { OnUpdate = function() while true do end end }");
	Entity healthy = AddScript(scene, "Healthy", "return { OnUpdate = function(e) e.Transform.Translation.x = e.Transform.Translation.x + 1 end }");
	AddScript(scene, "Recursive", "return { OnCreate = function() local function f() return f() + 1 end f() end }");

	ScriptEngine engine;
	CHECK(engine.GetInstructionLimit() == 5'000'000);
	engine.SetInstructionLimit(200'000);
	engine.Start(scene);
	engine.Update(0.0f); // must return, not hang
	engine.Update(0.0f);

	CHECK(capture.Contains(LogLevel::Error, "instruction limit"));
	CHECK(capture.Contains(LogLevel::Error, "Spinner"));
	CHECK(healthy.GetComponent<TransformComponent>().Translation.x == doctest::Approx(2.0f)); // other scripts keep running

	// Execute is budgeted per call, and a normal call afterwards works.
	CHECK_FALSE(engine.Execute("while true do end"));
	CHECK(engine.Execute("local n = 0 for i = 1, 1000 do n = n + i end assert(n == 500500)"));
	engine.SetInstructionLimit(100); // tiny budgets reject even short loops
	CHECK_FALSE(engine.Execute("for i = 1, 100000 do end"));
	CHECK(engine.GetInstructionLimit() == 100);
	engine.Stop();
}
