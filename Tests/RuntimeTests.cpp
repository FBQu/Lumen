#include <doctest/doctest.h>

#include "Lumen/Core/Input.h"
#include "Lumen/Core/Log.h"
#include "Lumen/Runtime/GameSession.h"
#include "Lumen/Runtime/Project.h"

#include <filesystem>
#include <fstream>

using namespace Lumen;
namespace fs = std::filesystem;

namespace {

	struct TempProject
	{
		fs::path Dir;

		explicit TempProject(const std::string& name)
		{
			Dir = fs::temp_directory_path() / ("lumen_project_" + name);
			fs::remove_all(Dir);
			fs::create_directories(Dir);
		}
		~TempProject() { fs::remove_all(Dir); }

		void Write(const std::string& relative, const std::string& text) const
		{
			fs::create_directories((Dir / relative).parent_path());
			std::ofstream(Dir / relative, std::ios::binary) << text;
		}
		std::string File(const std::string& relative = "game.lumen") const { return (Dir / relative).generic_string(); }
	};

	const char* kScene = R"({"version":1,"entities":[
		{"id":"1","name":"Camera","camera":{},"transform":{"translation":[0,2,6]}},
		{"id":"2","name":"Ball","transform":{"translation":[0,5,0]},"collider":{"shape":"sphere"},"rigidbody":{},
		 "script":{"source":"return { OnUpdate = function(self, dt) if Input.IsKeyDown('d') then Physics.AddForce(self, vec3.new(50, 0, 0)) end end }"}},
		{"id":"3","name":"Floor","transform":{"translation":[0,-0.5,0],"scale":[20,1,20]},"collider":{"halfExtents":[0.5,0.5,0.5]}}]})";

	struct ErrorLog
	{
		static inline int Count = 0;
		static void Sink(LogLevel level, std::string_view) { if (level == LogLevel::Error) Count++; }
	};

}

TEST_CASE("Project files: defaults, full settings and round trip")
{
	std::string error;
	auto minimal = Project::Parse("{}", &error);
	REQUIRE_MESSAGE(minimal.has_value(), error);
	CHECK(minimal->Scene == "scene.json");
	CHECK(minimal->Environment == "sky");
	CHECK(minimal->WindowWidth == 1280);
	CHECK(minimal->QuitOnEscape);
	CHECK(minimal->WindowTitle == minimal->Name);

	auto full = Project::Parse(R"({"name":"Tetris","scene":"levels/main.json","models":["models/a.gltf","b.glb"],"environment":"sky/dawn.hdr",
		"quitOnEscape":false,"window":{"title":"My Tetris","width":800,"height":600,"vsync":false},
		"renderer":{"exposure":1.5,"enableShadows":false,"ambient":[0.1,0.2,0.3],"clearColor":[0,0,0],"aoRadius":2}})", &error);
	REQUIRE_MESSAGE(full.has_value(), error);
	CHECK(full->Name == "Tetris");
	CHECK(full->Models == std::vector<std::string>{ "models/a.gltf", "b.glb" });
	CHECK_FALSE(full->QuitOnEscape);
	CHECK(full->WindowTitle == "My Tetris");
	CHECK(full->WindowWidth == 800);
	CHECK_FALSE(full->VSync);
	CHECK(full->Render.Exposure == doctest::Approx(1.5f));
	CHECK_FALSE(full->Render.EnableShadows);
	CHECK(full->Render.Ambient == glm::vec3(0.1f, 0.2f, 0.3f));
	CHECK(full->Render.AORadius == doctest::Approx(2.0f));

	auto again = Project::Parse(Project::Serialize(*full), &error);
	REQUIRE_MESSAGE(again.has_value(), error);
	CHECK(Project::Serialize(*again) == Project::Serialize(*full));
}

TEST_CASE("Project files are validated, and paths cannot escape the project")
{
	auto fails = [](const char* json, const char* fragment)
	{
		std::string error;
		const bool rejected = !Project::Parse(json, &error).has_value();
		if (!rejected || error.find(fragment) == std::string::npos)
			MESSAGE("input " << json << " -> '" << error << "'");
		return rejected && error.find(fragment) != std::string::npos;
	};
	CHECK(fails("nope", "invalid JSON"));
	CHECK(fails("[]", "JSON object"));
	CHECK(fails(R"({"name":5})", "'name' must be a string"));
	CHECK(fails(R"({"scene":"../outside.json"})", "inside the project"));
	CHECK(fails(R"({"scene":"/etc/passwd"})", "inside the project"));
	CHECK(fails(R"({"scene":"C:\\x.json"})", "inside the project"));
	CHECK(fails(R"({"models":"a.gltf"})", "'models' must be an array"));
	CHECK(fails(R"({"models":[5]})", "'models' must be an array"));
	CHECK(fails(R"({"models":["a/../../b.gltf"]})", "inside the project"));
	CHECK(fails(R"({"environment":"../x.hdr"})", "inside the project"));
	CHECK(fails(R"({"window":5})", "'window' must be an object"));
	CHECK(fails(R"({"window":{"width":10}})", "between 64 and 16384"));
	CHECK(fails(R"({"window":{"width":1280.5}})", "between 64 and 16384"));
	CHECK(fails(R"({"window":{"vsync":"yes"}})", "'vsync' must be a boolean"));
	CHECK(fails(R"({"quitOnEscape":1})", "'quitOnEscape' must be a boolean"));
	CHECK(fails(R"({"renderer":{"exposure":-1}})", "'exposure'"));
	CHECK(fails(R"({"renderer":{"ambient":[1,2]}})", "'ambient'"));
	CHECK(fails(R"({"renderer":5})", "'renderer' must be an object"));

	CHECK(Project::IsSafeRelativePath("a/b/c.json"));
	CHECK(Project::IsSafeRelativePath("a..b/c"));      // ".." inside a name is fine
	CHECK(Project::IsSafeRelativePath("./scene.json"));
	CHECK_FALSE(Project::IsSafeRelativePath(""));
	CHECK_FALSE(Project::IsSafeRelativePath("a/../b"));
	CHECK_FALSE(Project::IsSafeRelativePath(".."));
	CHECK_FALSE(Project::IsSafeRelativePath("\\\\server\\share"));
}

TEST_CASE("GameSession loads a project, simulates it and reacts to input")
{
	TempProject project("session");
	project.Write("game.lumen", R"({"name":"Test","environment":"none"})");
	project.Write("scene.json", kScene);

	GameSession session;
	std::string error;
	REQUIRE_MESSAGE(session.Load(project.File(), &error), error);
	CHECK(session.GetProject().Name == "Test");
	CHECK(session.GetEnvironment() == nullptr); // "none"
	CHECK(session.GetScene().GetEntityCount() == 3);
	CHECK(session.FindCamera().IsValid());
	CHECK_FALSE(session.Load(project.File(), &error)); // one project per session

	InputState input;
	session.GetScripts().SetInput(&input);
	session.Start();
	CHECK(session.IsRunning());
	for (int i = 0; i < 120; i++)
		session.Update(1.0f / 60.0f);
	Entity ball = session.GetScene().FindEntityByName("Ball");
	CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.1));
	CHECK(std::abs(ball.GetComponent<TransformComponent>().Translation.x) < 0.1f); // no input yet

	input.SetKey(Key::D, true);
	for (int i = 0; i < 60; i++)
		session.Update(1.0f / 60.0f);
	CHECK(ball.GetComponent<TransformComponent>().Translation.x > 1.0f); // pushed to the right by the script
	CHECK(session.GetFrame() == 180);

	// Long frames are clamped so one hitch cannot explode the simulation.
	const float before = ball.GetComponent<TransformComponent>().Translation.x;
	session.Update(5.0f);
	CHECK(std::abs(ball.GetComponent<TransformComponent>().Translation.x - before) < 5.0f);

	session.Stop();
	CHECK_FALSE(session.IsRunning());
	session.Update(1.0f); // ignored while stopped
	session.GetScripts().SetInput(nullptr);
}

TEST_CASE("GameSession reports every kind of broken project clearly")
{
	auto loadError = [](const TempProject& project)
	{
		GameSession session;
		std::string error;
		CHECK_FALSE(session.Load(project.File(), &error));
		return error;
	};

	{
		GameSession session;
		std::string error;
		CHECK_FALSE(session.Load("/no/such/game.lumen", &error));
		CHECK(error.find("could not read project file") != std::string::npos);
	}
	{
		TempProject p("broken1");
		p.Write("game.lumen", "{ not json");
		CHECK(loadError(p).find("invalid project file") != std::string::npos);
	}
	{
		TempProject p("broken2");
		p.Write("game.lumen", "{}");
		CHECK(loadError(p).find("could not read scene") != std::string::npos);
	}
	{
		TempProject p("broken3");
		p.Write("game.lumen", "{}");
		p.Write("scene.json", R"({"version":1,"entities":[{"id":"1","collider":{"shape":"torus"}}]})");
		CHECK(loadError(p).find("invalid scene") != std::string::npos);
	}
	{
		TempProject p("broken4");
		p.Write("game.lumen", R"({"models":["models/missing.gltf"]})");
		p.Write("scene.json", kScene);
		CHECK(loadError(p).find("could not import model") != std::string::npos);
	}
	{
		TempProject p("broken5");
		p.Write("game.lumen", R"({"environment":"missing.hdr"})");
		p.Write("scene.json", kScene);
		CHECK(loadError(p).find("could not read environment") != std::string::npos);
	}
	{
		TempProject p("broken6");
		p.Write("game.lumen", R"({"environment":"bad.hdr"})");
		p.Write("scene.json", kScene);
		p.Write("bad.hdr", "this is not an image");
		CHECK(loadError(p).find("could not decode environment") != std::string::npos);
	}
}

TEST_CASE("GameSession builds the procedural sky environment by default")
{
	TempProject project("sky");
	project.Write("game.lumen", "{}");
	project.Write("scene.json", R"({"version":1,"entities":[]})");
	ErrorLog::Count = 0;
	Log::SetSink(&ErrorLog::Sink);

	GameSession session;
	REQUIRE(session.Load(project.File()));
	Log::SetSink(nullptr);
	CHECK(session.GetEnvironment() != nullptr);
	CHECK(session.GetEnvironment()->Specular.MipCount > 3);
	CHECK_FALSE(session.FindCamera().IsValid()); // allowed, with a warning
	session.Start();
	session.Update(0.016f);
	CHECK(ErrorLog::Count == 0);
}

TEST_CASE("The Physics Playground sample project works: loads, plays, responds to controls")
{
	GameSession session;
	std::string error;
	REQUIRE_MESSAGE(session.Load(std::string(LUMEN_SOURCE_DIR) + "/Projects/PhysicsPlayground/game.lumen", &error), error);
	CHECK(session.GetScene().GetEntityCount() > 25);
	CHECK(session.FindCamera().IsValid());
	CHECK(session.GetScene().FindPrefab("Ball") != nullptr);

	ErrorLog::Count = 0;
	Log::SetSink(&ErrorLog::Sink);
	InputState input;
	session.GetScripts().SetInput(&input);
	session.Start();

	for (int i = 0; i < 180; i++) // let everything settle
		session.Update(1.0f / 60.0f);
	Entity player = session.GetScene().FindEntityByName("Player");
	REQUIRE(player.IsValid());
	CHECK(player.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.15));
	const glm::vec3 start = player.GetComponent<TransformComponent>().Translation;

	input.SetKey(Key::W, true); // roll forward (-Z)
	for (int i = 0; i < 90; i++)
		session.Update(1.0f / 60.0f);
	CHECK(player.GetComponent<TransformComponent>().Translation.z < start.z - 1.0f);

	input.SetKey(Key::W, false);
	input.BeginFrame();
	input.SetKey(Key::E, true); // drops a ball
	const size_t before = session.GetScene().GetEntityCount();
	session.Update(1.0f / 60.0f);
	CHECK(session.GetScene().GetEntityCount() == before + 1);
	input.SetKey(Key::E, false);

	input.BeginFrame();
	input.SetKey(Key::Space, true); // jump from the ground
	session.Update(1.0f / 60.0f);
	input.BeginFrame();
	input.SetKey(Key::Space, false);
	for (int i = 0; i < 10; i++)
		session.Update(1.0f / 60.0f);
	CHECK(player.GetComponent<TransformComponent>().Translation.y > 0.8f);

	// The camera script follows the player.
	for (int i = 0; i < 240; i++)
		session.Update(1.0f / 60.0f);
	const glm::vec3 camera = session.FindCamera().GetComponent<TransformComponent>().Translation;
	const glm::vec3 playerPosition = player.GetComponent<TransformComponent>().Translation;
	CHECK(glm::distance(camera, playerPosition + glm::vec3(0, 9, 11)) < 1.5f);

	Log::SetSink(nullptr);
	CHECK(ErrorLog::Count == 0); // no script errors
	session.Stop();
	session.GetScripts().SetInput(nullptr);
}

#include "Lumen/Runtime/ProjectExport.h"

TEST_CASE("Executable names are made filesystem safe")
{
	CHECK(MakeExecutableName("My Game") == "My_Game");
	CHECK(MakeExecutableName("Physics Playground") == "Physics_Playground");
	CHECK(MakeExecutableName("  weird/name?*<>|:\"v1.2  ") == "weirdnamev1_2");
	CHECK(MakeExecutableName("../../etc") == "etc");
	CHECK(MakeExecutableName("") == "Game");
	CHECK(MakeExecutableName("???") == "Game");
	CHECK(MakeExecutableName("日本語") == "Game");
	CHECK(MakeExecutableName("ok-name_1") == "ok-name_1");
}

TEST_CASE("Export copies the project and installs the runtime, skipping hidden files")
{
	TempProject project("export_src");
	project.Write("game.lumen", R"({"name":"Cool Game"})");
	project.Write("scene.json", kScene);
	project.Write("models/robot.gltf", "{}");
	project.Write("models/tex/albedo.png", "png");
	project.Write(".git/config", "secret");
	project.Write(".hidden.txt", "hidden");
	project.Write("Export/old_build.txt", "stale"); // an earlier export living inside the project
	TempProject runtime("export_rt");
	runtime.Write("LumenRuntime", "#!/bin/sh\necho hi\n");

	const fs::path out = project.Dir / "Export";
	std::string error;
	REQUIRE_MESSAGE(ExportProject(project.File(), out.generic_string(), (runtime.Dir / "LumenRuntime").generic_string(), &error), error);

	CHECK(fs::exists(out / "game.lumen"));
	CHECK(fs::exists(out / "scene.json"));
	CHECK(fs::exists(out / "models" / "robot.gltf"));
	CHECK(fs::exists(out / "models" / "tex" / "albedo.png"));
	CHECK_FALSE(fs::exists(out / ".git"));
	CHECK_FALSE(fs::exists(out / ".hidden.txt"));
	CHECK_FALSE(fs::exists(out / "Export")); // the destination is not copied into itself
	REQUIRE(fs::exists(out / "Cool_Game"));
	CHECK((fs::status(out / "Cool_Game").permissions() & fs::perms::owner_exec) != fs::perms::none);

	// The exported folder is a valid project of its own.
	GameSession session;
	CHECK_MESSAGE(session.Load((out / "game.lumen").generic_string(), &error), error);

	// Re-exporting overwrites in place.
	CHECK(ExportProject(project.File(), out.generic_string(), (runtime.Dir / "LumenRuntime").generic_string(), &error));
}

TEST_CASE("Export validates before touching anything")
{
	TempProject project("export_bad");
	TempProject runtime("export_bad_rt");
	runtime.Write("rt", "x");
	const std::string rt = (runtime.Dir / "rt").generic_string();
	const std::string out = (project.Dir / "out").generic_string();
	std::string error;

	CHECK_FALSE(ExportProject("/no/such/game.lumen", out, rt, &error));
	CHECK(error.find("could not read project file") != std::string::npos);

	project.Write("game.lumen", "{ broken");
	CHECK_FALSE(ExportProject(project.File(), out, rt, &error));
	CHECK(error.find("invalid project file") != std::string::npos);
	CHECK_FALSE(fs::exists(out));

	project.Write("game.lumen", "{}");
	CHECK_FALSE(ExportProject(project.File(), out, rt, &error)); // no scene yet
	CHECK(error.find("scene") != std::string::npos);
	CHECK_FALSE(fs::exists(out));

	project.Write("scene.json", kScene);
	CHECK_FALSE(ExportProject(project.File(), out, "/no/runtime", &error));
	CHECK(error.find("runtime executable") != std::string::npos);
	CHECK_FALSE(ExportProject(project.File(), project.Dir.generic_string(), rt, &error));
	CHECK(error.find("must differ") != std::string::npos);
	CHECK(ExportProject(project.File(), out, rt, &error)); // and a good export works
}
