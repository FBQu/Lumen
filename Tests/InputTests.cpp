#include <doctest/doctest.h>

#include "Lumen/Core/Input.h"
#include "Lumen/Core/Log.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"
#include "Lumen/Scripting/ScriptEngine.h"

using namespace Lumen;

namespace {
	struct ErrorCounter
	{
		static inline int Errors = 0;
		static void Sink(LogLevel level, std::string_view) { if (level == LogLevel::Error) Errors++; }
	};
}

TEST_CASE("InputState: down, pressed and released edges")
{
	InputState input;
	CHECK_FALSE(input.IsKeyDown(Key::W));

	input.SetKey(Key::W, true);
	CHECK(input.IsKeyDown(Key::W));
	CHECK(input.WasKeyPressed(Key::W));
	CHECK_FALSE(input.WasKeyReleased(Key::W));

	input.BeginFrame(); // a new frame: still down, but the press edge is gone
	CHECK(input.IsKeyDown(Key::W));
	CHECK_FALSE(input.WasKeyPressed(Key::W));

	input.SetKey(Key::W, true); // key repeat does not create a new press
	CHECK_FALSE(input.WasKeyPressed(Key::W));

	input.SetKey(Key::W, false);
	CHECK_FALSE(input.IsKeyDown(Key::W));
	CHECK(input.WasKeyReleased(Key::W));

	// A tap within one frame still shows both edges.
	input.BeginFrame();
	input.SetKey(Key::Space, true);
	input.SetKey(Key::Space, false);
	CHECK(input.WasKeyPressed(Key::Space));
	CHECK(input.WasKeyReleased(Key::Space));
	CHECK_FALSE(input.IsKeyDown(Key::Space));

	// Invalid keys are ignored safely.
	input.SetKey(Key::Unknown, true);
	input.SetKey(Key::Count, true);
	CHECK_FALSE(input.IsKeyDown(Key::Unknown));
	CHECK_FALSE(input.IsKeyDown(Key::Count));
}

TEST_CASE("InputState: mouse buttons, movement deltas, scroll and ReleaseAll")
{
	InputState input;
	input.SetMousePosition({ 100.0f, 50.0f });
	CHECK(input.GetMouseDelta() == glm::vec2(0.0f)); // the first position only sets the origin
	input.SetMousePosition({ 110.0f, 45.0f });
	input.SetMousePosition({ 112.0f, 45.0f });
	CHECK(input.GetMouseDelta() == glm::vec2(12.0f, -5.0f)); // accumulates within a frame
	CHECK(input.GetMousePosition() == glm::vec2(112.0f, 45.0f));
	input.AddScroll(1.0f);
	input.AddScroll(0.5f);
	CHECK(input.GetScroll() == doctest::Approx(1.5f));

	input.SetMouseButton(MouseButton::Right, true);
	CHECK(input.IsMouseButtonDown(MouseButton::Right));
	CHECK(input.WasMouseButtonPressed(MouseButton::Right));
	CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Left));

	input.BeginFrame();
	CHECK(input.GetMouseDelta() == glm::vec2(0.0f));
	CHECK(input.GetScroll() == 0.0f);
	CHECK(input.GetMousePosition() == glm::vec2(112.0f, 45.0f)); // position persists
	CHECK_FALSE(input.WasMouseButtonPressed(MouseButton::Right));

	input.SetKey(Key::A, true);
	input.ReleaseAll(); // e.g. focus lost
	CHECK_FALSE(input.IsKeyDown(Key::A));
	CHECK(input.WasKeyReleased(Key::A));
	CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Right));
	CHECK(input.WasMouseButtonReleased(MouseButton::Right));
}

TEST_CASE("Key and mouse button names")
{
	CHECK(InputState::KeyFromName("W") == Key::W);
	CHECK(InputState::KeyFromName("w") == Key::W);
	CHECK(InputState::KeyFromName("Space") == Key::Space);
	CHECK(InputState::KeyFromName("ESC") == Key::Escape);
	CHECK(InputState::KeyFromName("Shift") == Key::LeftShift);
	CHECK(InputState::KeyFromName("LeftControl") == Key::LeftControl);
	CHECK(InputState::KeyFromName("F5") == Key::F5);
	CHECK(InputState::KeyFromName("7") == Key::Num7);
	CHECK_FALSE(InputState::KeyFromName("banana").has_value());
	CHECK_FALSE(InputState::KeyFromName("").has_value());

	for (uint16_t k = 1; k < static_cast<uint16_t>(Key::Count); k++) // every key round-trips through its name
		CHECK(InputState::KeyFromName(InputState::KeyName(static_cast<Key>(k))) == static_cast<Key>(k));
	CHECK(std::string(InputState::KeyName(Key::Unknown)) == "unknown");

	CHECK(InputState::MouseButtonFromName("Left") == MouseButton::Left);
	CHECK(InputState::MouseButtonFromName("middle") == MouseButton::Middle);
	CHECK_FALSE(InputState::MouseButtonFromName("side").has_value());
}

TEST_CASE("Lua Input table reads the engine's input state")
{
	Scene scene;
	Entity player = scene.CreateEntity("Player");
	player.AddComponent<ScriptComponent>(R"(
		return { OnUpdate = function(self, dt)
			local t = self.Transform.Translation
			if Input.IsKeyDown("d") then t.x = t.x + 1 end
			if Input.IsKeyDown("a") then t.x = t.x - 1 end
			if Input.WasKeyPressed("space") then t.y = t.y + 10 end
			if Input.IsMouseButtonDown("left") then t.z = t.z + 1 end
			self.Transform.Translation.x = t.x + Input.GetMouseDeltaX() + Input.GetScroll() * 100
		end }
	)");

	InputState input;
	ScriptEngine engine;
	engine.Start(scene);

	engine.Update(0.0f); // no input attached: nothing pressed
	CHECK(player.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f));
	CHECK(engine.Execute("assert(not Input.IsKeyDown('w') and Input.GetMouseX() == 0 and Input.GetScroll() == 0)"));

	engine.SetInput(&input);
	input.SetKey(Key::D, true);
	input.SetKey(Key::Space, true);
	input.SetMouseButton(MouseButton::Left, true);
	input.SetMousePosition({ 10, 10 });
	input.SetMousePosition({ 15, 10 });
	input.AddScroll(1.0f);
	engine.Update(0.0f);
	CHECK(player.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f + 5.0f + 100.0f, 10.0f, 1.0f));

	input.BeginFrame(); // the space press was an edge: it does not repeat
	engine.Update(0.0f);
	CHECK(player.GetComponent<TransformComponent>().Translation.y == doctest::Approx(10.0f));

	// Bad names are script errors, not crashes.
	ErrorCounter::Errors = 0;
	Log::SetSink(&ErrorCounter::Sink);
	CHECK_FALSE(engine.Execute("Input.IsKeyDown('banana')"));
	CHECK_FALSE(engine.Execute("Input.IsMouseButtonDown('side')"));
	CHECK(ErrorCounter::Errors == 2);
	Log::SetSink(nullptr);

	engine.Stop();
	engine.SetInput(nullptr);
}
