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

// ---------------------------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------------------------

#include "GltfTestData.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <glm/glm.hpp>

TEST_CASE("Agent: import a glTF, instantiate it, and the references survive play and scene round trips")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "lumen_agent_assets";
	fs::create_directories(dir);
	const std::string path = (dir / "tri.gltf").string();
	std::ofstream(path) << GltfTestData::TriangleGltf();

	AgentSession session;
	Json imported = Ok(session, "asset.import_gltf", { { "path", path } });
	CHECK(imported["name"] == "tri.gltf");
	CHECK(imported["primitives"] == 1);
	CHECK(imported["warnings"].empty());
	const std::string model = imported["model"];

	CHECK(Ok(session, "asset.list")["meshes"] == 1);
	CHECK(Ok(session, "asset.import_gltf", { { "path", path } })["model"] == model); // idempotent
	CHECK(Ok(session, "asset.list")["meshes"] == 1);

	Json instances = Ok(session, "asset.instantiate", { { "model", model }, { "transform", { { "translation", { 5, 0, 0 } }, { "scale", { 2, 2, 2 } } } } });
	REQUIRE(instances["entities"].size() == 1);
	const std::string id = instances["entities"][0]["id"];

	Json entity = Ok(session, "entity.get", { { "id", id } });
	CHECK(entity["name"] == "Root");
	CHECK(entity["transform"]["translation"][0] == doctest::Approx(7.0)); // root T(5) * S(2) applied to the node's local x of 1
	CHECK(entity["transform"]["scale"][0] == doctest::Approx(2.0));
	CHECK(entity["meshRenderer"]["meshAsset"] != "0");
	CHECK(entity["meshRenderer"]["materialAsset"] != "0");

	// Scene JSON carries the asset references; reloading keeps them.
	Json scene = Ok(session, "scene.get");
	Ok(session, "scene.clear");
	Ok(session, "scene.load", { { "scene", scene } });
	CHECK(Ok(session, "entity.get", { { "id", id } })["meshRenderer"]["meshAsset"] == entity["meshRenderer"]["meshAsset"]);

	Ok(session, "play.start");
	Ok(session, "play.step", { { "frames", 2 } });
	Ok(session, "play.stop");
	CHECK(Ok(session, "entity.get", { { "id", id } })["meshRenderer"]["meshAsset"] == entity["meshRenderer"]["meshAsset"]);

	fs::remove_all(dir);
}

TEST_CASE("Agent: asset commands validate their input")
{
	AgentSession session;
	CHECK(Contains(ErrorOf(session, "asset.import_gltf"), "missing argument 'path'"));
	CHECK(Contains(ErrorOf(session, "asset.import_gltf", { { "path", 5 } }), "'path' must be a string"));
	CHECK(Contains(ErrorOf(session, "asset.import_gltf", { { "path", "/no/such/file.gltf" } }), "could not read"));
	CHECK(Contains(ErrorOf(session, "asset.instantiate"), "missing argument 'model'"));
	CHECK(Contains(ErrorOf(session, "asset.instantiate", { { "model", "123" } }), "model not found"));
	CHECK(Contains(ErrorOf(session, "asset.instantiate", { { "model", 5 } }), "'model' must be a decimal string"));
	CHECK(Ok(session, "asset.list")["meshes"] == 0);
}

TEST_CASE("Agent: render components can be created and patched through JSON")
{
	AgentSession session;
	const std::string id = Ok(session, "entity.create", { { "name", "Lamp" },
		{ "meshRenderer", { { "primitive", "sphere" }, { "material", { { "roughness", 0.1 }, { "emissive", { 1, 0, 0 } } } } } } })["id"];
	CHECK(Ok(session, "entity.get", { { "id", id } })["meshRenderer"]["material"]["roughness"] == doctest::Approx(0.1));

	Json patched = Ok(session, "entity.set", { { "id", id }, { "meshRenderer", { { "material", { { "metallic", 1.0 } } } } },
	                                           { "camera", { { "fovDegrees", 50 } } }, { "directionalLight", { { "intensity", 2 } } } });
	CHECK(patched["meshRenderer"]["primitive"] == "sphere"); // merge patch keeps untouched fields
	CHECK(patched["meshRenderer"]["material"]["metallic"] == doctest::Approx(1.0));
	CHECK(patched["meshRenderer"]["material"]["roughness"] == doctest::Approx(0.1));
	CHECK(patched["camera"]["fovDegrees"] == doctest::Approx(50.0));
	CHECK(patched["directionalLight"]["intensity"] == doctest::Approx(2.0));

	CHECK_FALSE(Ok(session, "entity.set", { { "id", id }, { "camera", nullptr } }).contains("camera"));
	CHECK(Contains(ErrorOf(session, "entity.set", { { "id", id }, { "camera", { { "fovDegrees", 500 } } } }), "between 1 and 179"));
}

// ---------------------------------------------------------------------------------------------
// Rendering commands
// ---------------------------------------------------------------------------------------------

#include "RenderTestUtil.h"
#include "Lumen/Assets/ImageIO.h"
#include "Lumen/Core/Base64.h"

namespace {

	void BuildRenderableScene(AgentSession& session)
	{
		Ok(session, "entity.create", { { "name", "Camera" }, { "camera", Json::object() }, { "transform", { { "translation", { 0, 0, 3 } } } } });
		Ok(session, "entity.create", { { "name", "Sun" }, { "directionalLight", { { "intensity", 3 } } } });
		Ok(session, "entity.create", { { "name", "Ball" }, { "meshRenderer", { { "primitive", "sphere" }, { "material", { { "roughness", 0.8 } } } } } });
	}

	float CenterLuminance(const ImageData& image)
	{
		const uint8_t* p = image.PixelAt(image.Width / 2, image.Height / 2);
		return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
	}

	ImageData DecodeInline(const Json& result)
	{
		auto bytes = Base64::Decode(result["png_base64"].get<std::string>());
		REQUIRE(bytes.has_value());
		auto image = ImageIO::DecodeLDR(bytes->data(), bytes->size());
		REQUIRE(image.has_value());
		return *image;
	}

}

TEST_CASE("Agent: render.screenshot validates its input before touching the GPU")
{
	AgentSession session;
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "x.png" } }), "no camera"));

	Ok(session, "entity.create", { { "name", "Camera" }, { "camera", Json::object() } });
	CHECK(Contains(ErrorOf(session, "render.screenshot"), "'path'"));
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", 5 } }), "'path' must be a string"));
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "x.png" }, { "width", 10 } }), "'width'"));
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "x.png" }, { "height", 100000 } }), "'height'"));
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "x.png" }, { "width", 128.5 } }), "'width'"));
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "x.png" }, { "camera", "999" } }), "camera"));
	const std::string notACamera = Ok(session, "entity.create", { { "name", "Plain" } })["id"];
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "x.png" }, { "camera", notACamera } }), "camera component"));
}

TEST_CASE("Agent: render.screenshot renders the scene to a PNG file and inline")
{
	if (!GetTestRenderDevice())
		return;

	namespace fs = std::filesystem;
	const std::string path = (fs::temp_directory_path() / "lumen_agent_shot.png").string();

	AgentSession session;
	BuildRenderableScene(session);
	Ok(session, "render.set", { { "ambient", { 0, 0, 0 } }, { "enableShadows", false } });

	Json result = Ok(session, "render.screenshot", { { "path", path }, { "width", 128 }, { "height", 96 }, { "inline", true } });
	CHECK(result["width"] == 128);
	CHECK(result["height"] == 96);
	CHECK_FALSE(result["device"].get<std::string>().empty());

	REQUIRE(fs::exists(path));
	std::ifstream file(path, std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
	file.close();
	fs::remove(path);
	auto fromFile = ImageIO::DecodeLDR(bytes.data(), bytes.size());
	REQUIRE(fromFile.has_value());
	CHECK(fromFile->Width == 128);
	CHECK(fromFile->Height == 96);
	CHECK(CenterLuminance(*fromFile) > 100.0f); // the lit sphere

	ImageData inlined = DecodeInline(result);
	CHECK(inlined.Pixels == fromFile->Pixels); // identical bytes both ways
	const uint8_t* corner = inlined.PixelAt(2, 2);
	CHECK(corner[0] < 60); // background is dark

	// Failing to write leaves a clear error.
	CHECK(Contains(ErrorOf(session, "render.screenshot", { { "path", "/no/such/dir/x.png" } }), "could not write"));
}

TEST_CASE("Agent: render.set validates atomically and changes what the next screenshot shows")
{
	if (!GetTestRenderDevice())
		return;

	AgentSession session;
	BuildRenderableScene(session);

	Json defaults = Ok(session, "render.set");
	CHECK(defaults["exposure"] == 1.0);
	CHECK(defaults["enableShadows"] == true);

	CHECK(Contains(ErrorOf(session, "render.set", { { "exposure", -1 } }), "'exposure'"));
	CHECK(Contains(ErrorOf(session, "render.set", { { "exposure", "bright" } }), "must be a number"));
	CHECK(Contains(ErrorOf(session, "render.set", { { "enableShadows", 1 } }), "must be a boolean"));
	CHECK(Contains(ErrorOf(session, "render.set", { { "ambient", { 1, 2 } } }), "'ambient'"));
	// A request with one bad field must not apply the good ones.
	CHECK(Contains(ErrorOf(session, "render.set", { { "exposure", 2.0 }, { "aoRadius", -5 } }), "'aoRadius'"));
	CHECK(Ok(session, "render.set")["exposure"] == 1.0);

	Ok(session, "render.set", { { "ambient", { 0, 0, 0 } } });
	const float normal = CenterLuminance(DecodeInline(Ok(session, "render.screenshot", { { "inline", true }, { "width", 64 }, { "height", 64 } })));
	CHECK(Ok(session, "render.set", { { "exposure", 0.2 } })["exposure"] == doctest::Approx(0.2));
	const float dim = CenterLuminance(DecodeInline(Ok(session, "render.screenshot", { { "inline", true }, { "width", 64 }, { "height", 64 } })));
	CHECK(dim < normal - 30.0f);
}

TEST_CASE("Agent: environments can be set, replaced and removed; the screenshot size can change in between")
{
	if (!GetTestRenderDevice())
		return;

	AgentSession session;
	BuildRenderableScene(session);
	Ok(session, "render.set", { { "ambient", { 0, 0, 0 } }, { "clearColor", { 1, 0, 0 } } });

	auto corner = [&](int size)
	{
		ImageData image = DecodeInline(Ok(session, "render.screenshot", { { "inline", true }, { "width", size }, { "height", size } }));
		const uint8_t* p = image.PixelAt(2, 2);
		return glm::vec3(p[0], p[1], p[2]);
	};

	const glm::vec3 plain = corner(64);
	CHECK(plain.r > plain.b + 100.0f); // the red clear color

	CHECK(Ok(session, "render.set_environment", { { "source", "sky" } })["environment"] == "sky");
	const glm::vec3 withSky = corner(64);
	CHECK(withSky.b > withSky.r - 30.0f); // sky colors replace the clear color
	CHECK(corner(96).b > 100.0f);       // re-created at a new size, environment preserved

	Ok(session, "render.set_environment", { { "source", "none" } });
	CHECK(corner(64).r > corner(64).b + 100.0f);

	CHECK(Contains(ErrorOf(session, "render.set_environment"), "missing argument 'source'"));
	CHECK(Contains(ErrorOf(session, "render.set_environment", { { "source", 5 } }), "'source' must be a string"));
	CHECK(Contains(ErrorOf(session, "render.set_environment", { { "source", "/no/such.hdr" } }), "could not read"));
	CHECK(Contains(ErrorOf(session, "render.set_environment", { { "source", "/proc/version" } }), "could not decode")); // exists but is not an HDR image
}

TEST_CASE("Agent: screenshots show the live play state")
{
	if (!GetTestRenderDevice())
		return;

	AgentSession session;
	Ok(session, "entity.create", { { "name", "Camera" }, { "camera", Json::object() }, { "transform", { { "translation", { 0, 0, 6 } } } } });
	Ok(session, "entity.create", { { "name", "Sun" }, { "directionalLight", Json::object() } });
	Ok(session, "entity.create", { { "name", "Ball" }, { "transform", { { "translation", { 0, 0, 0 } } } },
		{ "meshRenderer", { { "primitive", "sphere" }, { "material", { { "roughness", 0.8 } } } } },
		{ "collider", { { "shape", "sphere" } } }, { "rigidbody", Json::object() } });
	Ok(session, "render.set", { { "ambient", { 0, 0, 0 } }, { "enableShadows", false } });

	auto centerLuminance = [&]
	{
		return CenterLuminance(DecodeInline(Ok(session, "render.screenshot", { { "inline", true }, { "width", 64 }, { "height", 64 } })));
	};
	CHECK(centerLuminance() > 100.0f); // the ball sits at the center

	Ok(session, "play.start");
	Ok(session, "play.step", { { "frames", 90 } }); // it falls out of the view
	CHECK(centerLuminance() < 60.0f);
	Ok(session, "play.stop");
	CHECK(centerLuminance() > 100.0f); // edit scene restored
}

TEST_CASE("Agent: prefabs can be created, spawned, listed, inspected and deleted")
{
	AgentSession session;
	const std::string source = Ok(session, "entity.create", { { "name", "Crate" }, { "meshRenderer", { { "primitive", "cube" } } }, { "transform", { { "scale", { 2, 2, 2 } } } } })["id"];

	CHECK(Ok(session, "prefab.create", { { "name", "Crate" }, { "entity", source } })["name"] == "Crate");
	CHECK(Ok(session, "prefab.list") == Json::array({ "Crate" }));
	Json stored = Ok(session, "prefab.get", { { "name", "Crate" } });
	CHECK_FALSE(stored.contains("id"));
	CHECK(stored["transform"]["scale"][0] == 2.0);

	Json spawned = Ok(session, "prefab.spawn", { { "name", "Crate" }, { "overrides", { { "name", "Crate 2" }, { "transform", { { "translation", { 4, 0, 0 } } } } } } });
	CHECK(spawned["name"] == "Crate 2");
	CHECK(spawned["id"] != source);
	Json entity = Ok(session, "entity.get", { { "id", spawned["id"] } });
	CHECK(entity["transform"]["translation"][0] == 4.0);
	CHECK(entity["transform"]["scale"][0] == 2.0);
	CHECK(session.GetScene().GetEntityCount() == 2);

	// Prefabs travel with the scene JSON and survive a play session.
	Json scene = Ok(session, "scene.get");
	CHECK(scene["prefabs"].contains("Crate"));
	Ok(session, "play.start");
	Ok(session, "play.stop");
	CHECK(Ok(session, "prefab.list") == Json::array({ "Crate" }));
	Ok(session, "scene.clear");
	CHECK(Ok(session, "prefab.list").empty());
	Ok(session, "scene.load", { { "scene", scene } });
	CHECK(Ok(session, "prefab.list") == Json::array({ "Crate" }));

	CHECK(Ok(session, "prefab.delete", { { "name", "Crate" } })["prefabs"] == 0);
	CHECK(Contains(ErrorOf(session, "prefab.get", { { "name", "Crate" } }), "not found"));
	CHECK(Contains(ErrorOf(session, "prefab.delete", { { "name", "Crate" } }), "not found"));
	CHECK(Contains(ErrorOf(session, "prefab.spawn", { { "name", "Crate" } }), "does not exist"));
}

TEST_CASE("Agent: prefab commands validate their input; scripts spawn prefabs during play")
{
	AgentSession session;
	const std::string source = Ok(session, "entity.create", { { "name", "Orb" }, { "meshRenderer", { { "primitive", "sphere" } } } })["id"];

	CHECK(Contains(ErrorOf(session, "prefab.create"), "missing argument 'name'"));
	CHECK(Contains(ErrorOf(session, "prefab.create", { { "name", 5 }, { "entity", source } }), "'name' must be a string"));
	CHECK(Contains(ErrorOf(session, "prefab.create", { { "name", "Orb" } }), "missing argument 'entity'"));
	CHECK(Contains(ErrorOf(session, "prefab.create", { { "name", "Orb" }, { "entity", "999" } }), "not found"));
	CHECK(Contains(ErrorOf(session, "prefab.create", { { "name", "" }, { "entity", source } }), "1 to 128"));
	CHECK(Contains(ErrorOf(session, "prefab.spawn", { { "name", "Orb" }, { "overrides", 5 } }), "'overrides' must be an object"));
	Ok(session, "prefab.create", { { "name", "Orb" }, { "entity", source } });
	CHECK(Contains(ErrorOf(session, "prefab.spawn", { { "name", "Orb" }, { "overrides", { { "id", "7" } } } }), "assigned by the engine"));

	Ok(session, "entity.create", { { "name", "Spawner" }, { "script", { { "source",
		"return { OnUpdate = function(self) Scene.Spawn('Orb', { Name = 'Spawned', Translation = vec3.new(0, 5, 0) }) end }" } } } });
	Ok(session, "play.start");
	Ok(session, "play.step", { { "frames", 3 } });
	CHECK(Ok(session, "play.state")["entityCount"] == 5); // orb + spawner + 3 spawned
	Ok(session, "play.stop");
	CHECK(session.GetScene().GetEntityCount() == 2);       // spawned entities vanish with the play session
}
