#include <doctest/doctest.h>

#include "Lumen/Agent/AgentSession.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <nlohmann/json.hpp>

using namespace Lumen;
using Json = nlohmann::json;

namespace {

	Json Call(AgentSession& session, const std::string& cmd, const Json& args = Json::object())
	{
		return Json::parse(session.Execute(Json{ { "cmd", cmd }, { "args", args } }.dump()));
	}

	// Returns the result of a command that must succeed.
	Json Ok(AgentSession& session, const std::string& cmd, const Json& args = Json::object())
	{
		Json response = Call(session, cmd, args);
		INFO(cmd << " -> " << response.dump());
		REQUIRE(response["ok"] == true);
		return response["result"];
	}

	std::string ErrorOf(AgentSession& session, const std::string& cmd, const Json& args = Json::object())
	{
		Json response = Call(session, cmd, args);
		INFO(cmd << " -> " << response.dump());
		REQUIRE(response["ok"] == false);
		CHECK_FALSE(response.contains("result"));
		return response["error"].get<std::string>();
	}

	bool Contains(const std::string& text, const std::string& fragment) { return text.find(fragment) != std::string::npos; }

}

TEST_CASE("Protocol: ping, help, request ids and malformed requests")
{
	AgentSession session;

	Json pong = Json::parse(session.Execute(R"({"cmd":"ping","requestId":{"a":[1,2]}})"));
	CHECK(pong["ok"] == true);
	CHECK(pong["result"] == "pong");
	CHECK(pong["requestId"] == Json::parse(R"({"a":[1,2]})"));

	Json help = Ok(session, "help");
	CHECK(help.size() >= 15);

	CHECK(Contains(Json::parse(session.Execute("{broken"))["error"].get<std::string>(), "invalid JSON"));
	CHECK(Contains(Json::parse(session.Execute("[1]"))["error"].get<std::string>(), "must be a JSON object"));
	CHECK(Contains(Json::parse(session.Execute("{}"))["error"].get<std::string>(), "'cmd'"));
	CHECK(Contains(ErrorOf(session, "does.not.exist"), "unknown command"));
	CHECK(Contains(Json::parse(session.Execute(R"({"cmd":"ping","args":5})"))["error"].get<std::string>(), "'args'"));
}

TEST_CASE("Entity lifecycle through the API")
{
	AgentSession session;

	Json created = Ok(session, "entity.create", { { "name", "Crate" }, { "transform", { { "translation", { 1, 2, 3 } } } } });
	const std::string id = created["id"].get<std::string>();
	CHECK(session.GetScene().GetEntityCount() == 1);

	Json entity = Ok(session, "entity.get", { { "id", id } });
	CHECK(entity["name"] == "Crate");
	CHECK(entity["transform"]["translation"][1] == 2.0);
	CHECK_FALSE(entity.contains("rigidbody"));

	CHECK(Ok(session, "entity.find", { { "name", "Crate" } })["id"] == id);
	CHECK(Ok(session, "entity.list").size() == 1);

	// Patch: merge nested objects, add components, rename.
	Json patched = Ok(session, "entity.set", { { "id", id }, { "name", "Renamed" },
	                                           { "transform", { { "scale", { 2, 2, 2 } } } },
	                                           { "collider", { { "shape", "capsule" }, { "radius", 0.3 } } },
	                                           { "rigidbody", { { "mass", 4 } } } });
	CHECK(patched["name"] == "Renamed");
	CHECK(patched["transform"]["translation"][2] == 3.0); // untouched by the patch
	CHECK(patched["transform"]["scale"][0] == 2.0);
	CHECK(patched["collider"]["shape"] == "capsule");
	CHECK(patched["rigidbody"]["mass"] == 4.0);

	// null removes a component
	Json removed = Ok(session, "entity.set", { { "id", id }, { "rigidbody", nullptr } });
	CHECK_FALSE(removed.contains("rigidbody"));
	CHECK(removed.contains("collider"));

	CHECK(Ok(session, "entity.destroy", { { "id", id } })["entityCount"] == 0);
	CHECK(Contains(ErrorOf(session, "entity.get", { { "id", id } }), "not found"));
}

TEST_CASE("Entity commands validate their input")
{
	AgentSession session;
	const std::string id = Ok(session, "entity.create", { { "name", "E" } })["id"];

	CHECK(Contains(ErrorOf(session, "entity.get"), "missing argument 'id'"));
	CHECK(Contains(ErrorOf(session, "entity.get", { { "id", 5 } }), "decimal string"));
	CHECK(Contains(ErrorOf(session, "entity.get", { { "id", "0" } }), "non-zero"));
	CHECK(Contains(ErrorOf(session, "entity.get", { { "id", "12abc" } }), "non-zero"));
	CHECK(Contains(ErrorOf(session, "entity.find", { { "name", "ghost" } }), "not found"));
	CHECK(Contains(ErrorOf(session, "entity.find", { { "name", 3 } }), "'name' must be a string"));
	CHECK(Contains(ErrorOf(session, "entity.create", { { "id", "9" } }), "assigned by the engine"));
	CHECK(Contains(ErrorOf(session, "entity.create", { { "collider", { { "shape", "torus" } } } }), "unknown collider shape"));
	CHECK(Contains(ErrorOf(session, "entity.set", { { "id", id }, { "rigidbody", { { "type", "x" } } } }), "unknown body type"));
	CHECK(Contains(ErrorOf(session, "entity.set", { { "id", id }, { "transform", { { "scale", { 1, 2 } } } } }), "3 numbers"));

	// A rejected patch must not partially apply.
	CHECK(Ok(session, "entity.get", { { "id", id } })["name"] == "E");
	CHECK(Contains(ErrorOf(session, "entity.set", { { "id", id }, { "name", "Changed" }, { "collider", { { "shape", "torus" } } } }), "torus"));
	CHECK(Ok(session, "entity.get", { { "id", id } })["name"] == "E");
	CHECK(session.GetScene().GetEntityCount() == 1); // failed creates left nothing behind
}

TEST_CASE("Scene get/load round trip, atomic load failure, clear")
{
	AgentSession session;
	Ok(session, "entity.create", { { "name", "A" } });
	Ok(session, "entity.create", { { "name", "B" }, { "collider", { { "shape", "sphere" } } } });

	Json scene = Ok(session, "scene.get");
	CHECK(scene["version"] == 1);
	CHECK(scene["entities"].size() == 2);

	Ok(session, "scene.clear");
	CHECK(session.GetScene().GetEntityCount() == 0);

	CHECK(Ok(session, "scene.load", { { "scene", scene } })["entityCount"] == 2);
	CHECK(Ok(session, "entity.find", { { "name", "B" } }).contains("id"));

	// A bad scene is rejected and the current scene is kept.
	Json bad = scene;
	bad["entities"][1]["collider"]["shape"] = "torus";
	CHECK(Contains(ErrorOf(session, "scene.load", { { "scene", bad } }), "invalid scene"));
	CHECK(session.GetScene().GetEntityCount() == 2);
	CHECK(Contains(ErrorOf(session, "scene.load"), "missing argument 'scene'"));
}

TEST_CASE("Play mode: edit scene is restored on stop, edit-only commands are blocked")
{
	AgentSession session;
	Ok(session, "entity.create", { { "name", "Ground" }, { "transform", { { "translation", { 0, -0.5, 0 } } } },
	                               { "collider", { { "halfExtents", { 50, 0.5, 50 } } } } });
	const std::string ball = Ok(session, "entity.create", { { "name", "Ball" }, { "transform", { { "translation", { 0, 5, 0 } } } },
	                                                       { "collider", { { "shape", "sphere" } } }, { "rigidbody", { { "type", "dynamic" } } } })["id"];

	CHECK(Contains(ErrorOf(session, "play.step"), "requires play mode"));
	CHECK(Contains(ErrorOf(session, "play.stop"), "requires play mode"));
	CHECK(Contains(ErrorOf(session, "script.eval", { { "code", "return 1" } }), "requires play mode"));

	CHECK(Ok(session, "play.start")["playing"] == true);
	CHECK(session.IsPlaying());
	CHECK(Contains(ErrorOf(session, "play.start"), "already playing"));
	CHECK(Contains(ErrorOf(session, "scene.clear"), "not available while playing"));
	CHECK(Contains(ErrorOf(session, "scene.load", { { "scene", Json::object() } }), "not available while playing"));

	CHECK(Ok(session, "play.step", { { "frames", 180 } })["frame"] == 180);
	CHECK(Ok(session, "play.state")["frame"] == 180);

	// The ball fell and came to rest on the ground.
	const double y = Ok(session, "entity.get", { { "id", ball } })["transform"]["translation"][1];
	CHECK(y == doctest::Approx(0.5).epsilon(0.1));

	// Entities spawned during play are discarded on stop.
	Ok(session, "entity.create", { { "name", "Temp" } });
	CHECK(Ok(session, "play.stop")["entityCount"] == 2);
	CHECK_FALSE(session.IsPlaying());

	// Edit scene restored exactly, including the ball's original position and id.
	Json restored = Ok(session, "entity.get", { { "id", ball } });
	CHECK(restored["transform"]["translation"][1] == 5.0);
	CHECK(Contains(ErrorOf(session, "entity.find", { { "name", "Temp" } }), "not found"));
}

TEST_CASE("play.step validates its arguments")
{
	AgentSession session;
	Ok(session, "play.start");
	CHECK(Contains(ErrorOf(session, "play.step", { { "frames", 0 } }), "'frames'"));
	CHECK(Contains(ErrorOf(session, "play.step", { { "frames", 1.5 } }), "'frames'"));
	CHECK(Contains(ErrorOf(session, "play.step", { { "frames", 1000000 } }), "'frames'"));
	CHECK(Contains(ErrorOf(session, "play.step", { { "dt", 0 } }), "'dt'"));
	CHECK(Contains(ErrorOf(session, "play.step", { { "dt", 5 } }), "'dt'"));
	CHECK(Contains(ErrorOf(session, "play.step", { { "dt", "fast" } }), "'dt'"));
	CHECK(Ok(session, "play.step", { { "frames", 3 }, { "dt", 0.5 } })["frame"] == 3);
	Ok(session, "play.stop");
}

TEST_CASE("Scripts run during play; eval, raycast and log are available")
{
	AgentSession session;
	Ok(session, "entity.create", { { "name", "Floor" }, { "transform", { { "translation", { 0, -0.5, 0 } } } },
	                               { "collider", { { "halfExtents", { 10, 0.5, 10 } } } } });
	const std::string mover = Ok(session, "entity.create", { { "name", "Mover" },
		{ "script", { { "source", "return { OnUpdate = function(self, dt) self.Transform.Translation.x = self.Transform.Translation.x + 1 end }" } } } })["id"];
	Ok(session, "entity.create", { { "name", "Bad" }, { "script", { { "source", "return { OnCreate = function() error('script exploded') end }" } } } });

	Ok(session, "play.start");
	Ok(session, "play.step", { { "frames", 5 } });
	CHECK(Ok(session, "entity.get", { { "id", mover } })["transform"]["translation"][0] == 5.0);

	CHECK(Ok(session, "script.eval", { { "code", "return Scene.GetEntityCount()" } }) == "3");
	CHECK(Ok(session, "script.eval", { { "code", "x = 1" } }).is_null());
	CHECK(Contains(ErrorOf(session, "script.eval", { { "code", "error('nope')" } }), "nope"));
	CHECK(Contains(ErrorOf(session, "script.eval", { { "code", "this is bad" } }), "syntax error"));
	CHECK(Contains(ErrorOf(session, "script.eval", { { "code", 5 } }), "'code' must be a string"));

	Json hit = Ok(session, "physics.raycast", { { "origin", { 0, 5, 0 } }, { "direction", { 0, -1, 0 } }, { "maxDistance", 20 } });
	CHECK(hit["distance"] == doctest::Approx(5.0).epsilon(0.001));
	CHECK(hit["normal"][1] == doctest::Approx(1.0));
	CHECK(Ok(session, "physics.raycast", { { "origin", { 50, 5, 0 } }, { "direction", { 0, -1, 0 } }, { "maxDistance", 20 } }).is_null());
	CHECK(Contains(ErrorOf(session, "physics.raycast", { { "origin", { 0, 5 } }, { "direction", { 0, -1, 0 } }, { "maxDistance", 20 } }), "'origin'"));
	CHECK(Contains(ErrorOf(session, "physics.raycast", { { "origin", { 0, 5, 0 } }, { "direction", { 0, -1, 0 } } }), "maxDistance"));

	// Script errors are visible to the agent through the log buffer.
	Json log = Ok(session, "log.get", { { "clear", true } });
	bool sawError = false;
	for (const Json& line : log)
		sawError |= line["level"] == "error" && Contains(line["message"], "script exploded");
	CHECK(sawError);
	CHECK(Ok(session, "log.get").empty());

	Ok(session, "play.stop");
}

TEST_CASE("Destroying a session while playing is safe")
{
	{
		AgentSession session;
		Ok(session, "entity.create", { { "name", "X" }, { "collider", { { "shape", "sphere" } } }, { "rigidbody", Json::object() },
		                               { "script", { { "source", "return {}" } } } });
		Ok(session, "play.start");
		Ok(session, "play.step", { { "frames", 10 } });
	}
	AgentSession again; // can create a fresh session afterwards
	CHECK(Ok(again, "ping") == "pong");
}
