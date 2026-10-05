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
