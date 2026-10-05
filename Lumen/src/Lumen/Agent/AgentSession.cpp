#include "Lumen/Agent/AgentSession.h"

#include "Lumen/Core/Log.h"
#include "Lumen/Physics/PhysicsWorld.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"
#include "Lumen/Scene/SceneSerializer.h"
#include "Lumen/Scripting/ScriptEngine.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>

namespace Lumen {

	using Json = nlohmann::json;

	namespace {

		struct CommandError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		constexpr size_t s_MaxLogLines = 500;
		constexpr int s_MaxStepFrames = 100000;

		const char* LevelName(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace: return "trace";
				case LogLevel::Info:  return "info";
				case LogLevel::Warn:  return "warn";
				case LogLevel::Error: return "error";
				case LogLevel::Fatal: return "fatal";
			}
			return "info";
		}

		UUID ParseUUID(const Json& value)
		{
			if (!value.is_string())
				throw CommandError("'id' must be a decimal string");
			const std::string& text = value.get_ref<const std::string&>();
			uint64_t number = 0;
			auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), number);
			if (text.empty() || ec != std::errc() || end != text.data() + text.size() || number == 0)
				throw CommandError("'id' must be a non-zero unsigned 64-bit decimal string");
			return UUID(number);
		}

		glm::vec3 ParseVec3(const Json& json, const char* what)
		{
			if (!json.is_array() || json.size() != 3 || !json[0].is_number() || !json[1].is_number() || !json[2].is_number())
				throw CommandError(std::string("'") + what + "' must be an array of 3 numbers");
			return glm::vec3(json[0].get<float>(), json[1].get<float>(), json[2].get<float>());
		}

		const Json& Require(const Json& args, const char* key)
		{
			auto it = args.find(key);
			if (it == args.end())
				throw CommandError(std::string("missing argument '") + key + "'");
			return *it;
		}

		std::string IDString(UUID id) { return std::to_string(static_cast<uint64_t>(id)); }

	}

	struct AgentSession::Impl
	{
		static inline Impl* s_Active = nullptr;

		Scene SceneData;
		PhysicsWorld Physics;
		ScriptEngine Scripts;

		bool Playing = false;
		uint64_t Frame = 0;
		std::string EditSnapshot;

		std::mutex LogMutex;
		std::deque<std::pair<LogLevel, std::string>> LogLines;

		using Handler = std::function<Json(const Json& args)>;
		std::map<std::string, Handler> Commands;

		static void LogSink(LogLevel level, std::string_view message)
		{
			if (s_Active == nullptr)
				return;
			std::lock_guard lock(s_Active->LogMutex);
			s_Active->LogLines.emplace_back(level, std::string(message));
			if (s_Active->LogLines.size() > s_MaxLogLines)
				s_Active->LogLines.pop_front();
		}

		Entity RequireEntity(const Json& args)
		{
			Entity entity = SceneData.FindEntityByUUID(ParseUUID(Require(args, "id")));
			if (!entity.IsValid())
				throw CommandError("entity not found");
			return entity;
		}

		void RequireEditing(const char* command)
		{
			if (Playing)
				throw CommandError(std::string(command) + " is not available while playing; use play.stop first");
		}

		void RequirePlaying(const char* command)
		{
			if (!Playing)
				throw CommandError(std::string(command) + " requires play mode; use play.start first");
		}

		void StopPlaying()
		{
			Scripts.Stop();
			Physics.Stop();
			SceneData.Clear();
			std::string error;
			if (!SceneSerializer::Deserialize(SceneData, EditSnapshot, &error))
				LM_ERROR("Failed to restore the edit scene: {}", error); // unreachable: the snapshot was serialized by us
			EditSnapshot.clear();
			Playing = false;
			Frame = 0;
		}

		Impl()
		{
			s_Active = this;
			Log::SetSink(&Impl::LogSink);
			RegisterCommands();
		}

		~Impl()
		{
			if (Playing)
				StopPlaying();
			Log::SetSink(nullptr);
			s_Active = nullptr;
		}

		void RegisterCommands()
		{
			Commands["ping"] = [](const Json&) { return Json("pong"); };

			Commands["scene.get"] = [this](const Json&) { return Json::parse(SceneSerializer::Serialize(SceneData)); };

			Commands["scene.load"] = [this](const Json& args)
			{
				RequireEditing("scene.load");
				const std::string previous = SceneSerializer::Serialize(SceneData);
				SceneData.Clear();
				std::string error;
				if (!SceneSerializer::Deserialize(SceneData, Require(args, "scene").dump(), &error))
				{
					SceneSerializer::Deserialize(SceneData, previous);
					throw CommandError("invalid scene: " + error);
				}
				return Json{ { "entityCount", SceneData.GetEntityCount() } };
			};

			Commands["scene.clear"] = [this](const Json&)
			{
				RequireEditing("scene.clear");
				SceneData.Clear();
				return Json{ { "entityCount", 0 } };
			};

			Commands["entity.list"] = [this](const Json&)
			{
				Json list = Json::array();
				for (auto [handle, id, tag] : SceneData.GetRegistry().view<const IDComponent, const TagComponent>().each())
					list.push_back({ { "id", IDString(id.ID) }, { "name", tag.Tag } });
				return list;
			};

			Commands["entity.find"] = [this](const Json& args)
			{
				const Json& name = Require(args, "name");
				if (!name.is_string())
					throw CommandError("'name' must be a string");
				Entity entity = SceneData.FindEntityByName(name.get<std::string>());
				if (!entity.IsValid())
					throw CommandError("entity not found");
				return Json{ { "id", IDString(entity.GetUUID()) } };
			};

			Commands["entity.create"] = [this](const Json& args)
			{
				Json data = args.is_object() ? args : Json::object();
				if (data.contains("id"))
					throw CommandError("'id' is assigned by the engine and cannot be specified");

				const UUID id;
				data["id"] = IDString(id);
				std::string error;
				const Json scene = { { "version", SceneSerializer::s_FormatVersion }, { "entities", Json::array({ data }) } };
				if (!SceneSerializer::Deserialize(SceneData, scene.dump(), &error))
					throw CommandError(error);
				return Json{ { "id", IDString(id) } };
			};

			Commands["entity.get"] = [this](const Json& args)
			{
				return Json::parse(SceneSerializer::SerializeEntity(SceneData, RequireEntity(args)));
			};

			Commands["entity.set"] = [this](const Json& args)
			{
				Entity entity = RequireEntity(args);
				Json patch = args;
				patch.erase("id");
				std::string error;
				if (!SceneSerializer::PatchEntity(SceneData, entity, patch.dump(), &error))
					throw CommandError(error);
				return Json::parse(SceneSerializer::SerializeEntity(SceneData, entity));
			};

			Commands["entity.destroy"] = [this](const Json& args)
			{
				SceneData.DestroyEntity(RequireEntity(args));
				return Json{ { "entityCount", SceneData.GetEntityCount() } };
			};

			Commands["play.start"] = [this](const Json&)
			{
				if (Playing)
					throw CommandError("already playing");
				EditSnapshot = SceneSerializer::Serialize(SceneData);
				Physics.Start(SceneData);
				Scripts.Start(SceneData, &Physics);
				Playing = true;
				Frame = 0;
				return Json{ { "playing", true } };
			};

			Commands["play.stop"] = [this](const Json&)
			{
				RequirePlaying("play.stop");
				StopPlaying();
				return Json{ { "playing", false }, { "entityCount", SceneData.GetEntityCount() } };
			};

			Commands["play.state"] = [this](const Json&)
			{
				return Json{ { "playing", Playing }, { "frame", Frame }, { "entityCount", SceneData.GetEntityCount() } };
			};

			Commands["play.step"] = [this](const Json& args)
			{
				RequirePlaying("play.step");
				int frames = 1;
				float dt = PhysicsWorld::s_FixedTimestep;
				if (auto it = args.find("frames"); it != args.end())
				{
					if (!it->is_number_integer() || it->get<int>() < 1 || it->get<int>() > s_MaxStepFrames)
						throw CommandError("'frames' must be an integer between 1 and " + std::to_string(s_MaxStepFrames));
					frames = it->get<int>();
				}
				if (auto it = args.find("dt"); it != args.end())
				{
					if (!it->is_number() || !(it->get<float>() > 0.0f) || it->get<float>() > 1.0f)
						throw CommandError("'dt' must be a number in (0, 1]");
					dt = it->get<float>();
				}

				for (int i = 0; i < frames; i++)
				{
					Scripts.Update(dt);
					Physics.Step(dt);
					Frame++;
				}
				return Json{ { "frame", Frame }, { "entityCount", SceneData.GetEntityCount() } };
			};

			Commands["script.eval"] = [this](const Json& args)
			{
				RequirePlaying("script.eval");
				const Json& code = Require(args, "code");
				if (!code.is_string())
					throw CommandError("'code' must be a string");

				std::string output, error;
				if (!Scripts.Execute(code.get<std::string>(), &output, &error))
					throw CommandError(error);
				return output.empty() ? Json(nullptr) : Json(output);
			};

			Commands["physics.raycast"] = [this](const Json& args)
			{
				RequirePlaying("physics.raycast");
				const glm::vec3 origin = ParseVec3(Require(args, "origin"), "origin");
				const glm::vec3 direction = ParseVec3(Require(args, "direction"), "direction");
				const Json& distance = Require(args, "maxDistance");
				if (!distance.is_number())
					throw CommandError("'maxDistance' must be a number");

				auto hit = Physics.Raycast(origin, direction, distance.get<float>());
				if (!hit)
					return Json(nullptr);
				return Json{ { "entity", IDString(hit->HitEntity.GetUUID()) },
				             { "point", { hit->Point.x, hit->Point.y, hit->Point.z } },
				             { "normal", { hit->Normal.x, hit->Normal.y, hit->Normal.z } },
				             { "distance", hit->Distance } };
			};

			Commands["log.get"] = [this](const Json& args)
			{
				std::lock_guard lock(LogMutex);
				Json lines = Json::array();
				for (auto& [level, message] : LogLines)
					lines.push_back({ { "level", LevelName(level) }, { "message", message } });
				if (args.value("clear", false))
					LogLines.clear();
				return lines;
			};

			Commands["help"] = [this](const Json&)
			{
				Json names = Json::array();
				for (auto& [name, handler] : Commands)
					names.push_back(name);
				return names;
			};
		}
	};

	AgentSession::AgentSession() : m_Impl(CreateScope<Impl>()) {}
	AgentSession::~AgentSession() = default;

	Scene& AgentSession::GetScene() { return m_Impl->SceneData; }
	bool AgentSession::IsPlaying() const { return m_Impl->Playing; }

	std::string AgentSession::Execute(std::string_view requestJson)
	{
		Json response;
		try
		{
			Json request = Json::parse(requestJson);
			if (!request.is_object())
				throw CommandError("request must be a JSON object");
			if (auto id = request.find("requestId"); id != request.end())
				response["requestId"] = *id;

			auto cmd = request.find("cmd");
			if (cmd == request.end() || !cmd->is_string())
				throw CommandError("request needs a string 'cmd'");

			auto handler = m_Impl->Commands.find(cmd->get<std::string>());
			if (handler == m_Impl->Commands.end())
				throw CommandError("unknown command '" + cmd->get<std::string>() + "' (use 'help' to list commands)");

			Json args = request.value("args", Json::object());
			if (!args.is_object())
				throw CommandError("'args' must be an object");

			response["result"] = handler->second(args);
			response["ok"] = true;
		}
		catch (const Json::exception& e)
		{
			response["ok"] = false;
			response["error"] = std::string("invalid JSON: ") + e.what();
			response.erase("result");
		}
		catch (const std::exception& e)
		{
			response["ok"] = false;
			response["error"] = e.what();
			response.erase("result");
		}
		return response.dump();
	}

}
