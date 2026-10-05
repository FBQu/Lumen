#include "Lumen/Scripting/ScriptEngine.h"

#include "Lumen/Core/Log.h"
#include "Lumen/Physics/PhysicsWorld.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <sol/sol.hpp>

#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Lumen {

	namespace {

		// Lua integers are signed 64-bit; UUIDs round-trip through a bit-for-bit cast.
		int64_t ToLuaID(UUID id) { return static_cast<int64_t>(static_cast<uint64_t>(id)); }
		UUID FromLuaID(int64_t value) { return UUID(static_cast<uint64_t>(value)); }

		void BindMath(sol::state& lua)
		{
			lua.new_usertype<glm::vec3>("vec3",
				sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
				"x", sol::property([](const glm::vec3& v) { return v.x; }, [](glm::vec3& v, float x) { v.x = x; }),
				"y", sol::property([](const glm::vec3& v) { return v.y; }, [](glm::vec3& v, float y) { v.y = y; }),
				"z", sol::property([](const glm::vec3& v) { return v.z; }, [](glm::vec3& v, float z) { v.z = z; }),
				"Length", [](const glm::vec3& v) { return glm::length(v); },
				"Normalized", [](const glm::vec3& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; },
				sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
				sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
				sol::meta_function::multiplication,
					sol::overload(
						[](const glm::vec3& a, float s) { return a * s; },
						[](float s, const glm::vec3& a) { return a * s; },
						[](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
				sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
				sol::meta_function::to_string, [](const glm::vec3& v) { return std::format("vec3({}, {}, {})", v.x, v.y, v.z); });
		}

		void BindTransform(sol::state& lua)
		{
			lua.new_usertype<TransformComponent>("TransformComponent",
				sol::no_constructor,
				"Translation", sol::property(
					[](TransformComponent& t) -> glm::vec3& { return t.Translation; },
					[](TransformComponent& t, const glm::vec3& v) { t.Translation = v; }),
				"Rotation", sol::property(
					[](TransformComponent& t) -> glm::vec3& { return t.Rotation; },
					[](TransformComponent& t, const glm::vec3& v) { t.Rotation = v; }),
				"Scale", sol::property(
					[](TransformComponent& t) -> glm::vec3& { return t.Scale; },
					[](TransformComponent& t, const glm::vec3& v) { t.Scale = v; }));
		}

		void BindEntity(sol::state& lua)
		{
			lua.new_usertype<Entity>("Entity",
				sol::no_constructor,
				"IsValid", [](const Entity& e) { return e.IsValid(); },
				"ID", sol::property([](const Entity& e) { return ToLuaID(e.GetUUID()); }),
				"Name", sol::property(
					[](const Entity& e) { return e.GetName(); },
					[](Entity& e, const std::string& name) { e.GetComponent<TagComponent>().Tag = name; }),
				"Transform", sol::property([](Entity& e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); }),
				sol::meta_function::equal_to, [](const Entity& a, const Entity& b) { return a == b; });
		}

		void BindLog(sol::state& lua)
		{
			sol::table log = lua.create_named_table("Log");
			log["Trace"] = [](const std::string& message) { Log::Write(LogLevel::Trace, message); };
			log["Info"] = [](const std::string& message) { Log::Write(LogLevel::Info, message); };
			log["Warn"] = [](const std::string& message) { Log::Write(LogLevel::Warn, message); };
			log["Error"] = [](const std::string& message) { Log::Write(LogLevel::Error, message); };
		}

		BodyType ParseBodyType(const std::string& name)
		{
			if (name == "static")    return BodyType::Static;
			if (name == "kinematic") return BodyType::Kinematic;
			if (name == "dynamic")   return BodyType::Dynamic;
			throw std::runtime_error("invalid body type '" + name + "' (expected static, kinematic or dynamic)");
		}

		ColliderShape ParseShape(const std::string& name)
		{
			if (name == "box")     return ColliderShape::Box;
			if (name == "sphere")  return ColliderShape::Sphere;
			if (name == "capsule") return ColliderShape::Capsule;
			throw std::runtime_error("invalid collider shape '" + name + "' (expected box, sphere or capsule)");
		}

		void BindPhysics(sol::state& lua, PhysicsWorld* physics)
		{
			lua["Entity"]["AddRigidbody"] = [](Entity& entity, sol::optional<sol::table> options)
			{
				RigidbodyComponent rigidbody;
				if (options)
				{
					sol::table o = *options;
					if (sol::optional<std::string> type = o["Type"])
						rigidbody.Type = ParseBodyType(*type);
					rigidbody.Mass = o.get_or("Mass", rigidbody.Mass);
					rigidbody.Friction = o.get_or("Friction", rigidbody.Friction);
					rigidbody.Restitution = o.get_or("Restitution", rigidbody.Restitution);
					rigidbody.GravityScale = o.get_or("GravityScale", rigidbody.GravityScale);
					rigidbody.LinearDamping = o.get_or("LinearDamping", rigidbody.LinearDamping);
					rigidbody.AngularDamping = o.get_or("AngularDamping", rigidbody.AngularDamping);
					rigidbody.FixedRotation = o.get_or("FixedRotation", rigidbody.FixedRotation);
				}
				entity.AddOrReplaceComponent<RigidbodyComponent>(rigidbody);
			};

			lua["Entity"]["AddCollider"] = [](Entity& entity, sol::optional<sol::table> options)
			{
				ColliderComponent collider;
				if (options)
				{
					sol::table o = *options;
					if (sol::optional<std::string> shape = o["Shape"])
						collider.Shape = ParseShape(*shape);
					collider.HalfExtents = o.get_or("HalfExtents", collider.HalfExtents);
					collider.Radius = o.get_or("Radius", collider.Radius);
					collider.HalfHeight = o.get_or("HalfHeight", collider.HalfHeight);
					collider.Offset = o.get_or("Offset", collider.Offset);
				}
				entity.AddOrReplaceComponent<ColliderComponent>(collider);
			};

			lua["Entity"]["HasRigidbody"] = [](const Entity& entity) { return entity.HasComponent<RigidbodyComponent>(); };
			lua["Entity"]["HasCollider"] = [](const Entity& entity) { return entity.HasComponent<ColliderComponent>(); };

			if (physics == nullptr)
				return;

			sol::table table = lua.create_named_table("Physics");
			table["SetGravity"] = [physics](const glm::vec3& gravity) { physics->SetGravity(gravity); };
			table["GetGravity"] = [physics]() { return physics->GetGravity(); };
			table["AddForce"] = [physics](const Entity& e, const glm::vec3& force) { return physics->AddForce(e, force); };
			table["AddImpulse"] = [physics](const Entity& e, const glm::vec3& impulse) { return physics->AddImpulse(e, impulse); };
			table["SetVelocity"] = [physics](const Entity& e, const glm::vec3& velocity) { return physics->SetLinearVelocity(e, velocity); };
			table["GetVelocity"] = [physics](const Entity& e) { return physics->GetLinearVelocity(e); };
			table["Raycast"] = [physics](sol::this_state state, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> sol::object
			{
				sol::state_view lua(state);
				std::optional<RaycastHit> hit = physics->Raycast(origin, direction, maxDistance);
				if (!hit)
					return sol::nil;

				sol::table result = lua.create_table();
				result["Entity"] = hit->HitEntity;
				result["Point"] = hit->Point;
				result["Normal"] = hit->Normal;
				result["Distance"] = hit->Distance;
				return result;
			};
		}

		void BindScene(sol::state& lua, Scene* scene)
		{
			sol::table table = lua.create_named_table("Scene");
			table["CreateEntity"] = [scene](sol::optional<std::string> name) { return scene->CreateEntity(name.value_or("Entity")); };
			table["DestroyEntity"] = [scene](const Entity& entity) { scene->DestroyEntity(entity); };
			table["FindEntityByName"] = [scene](const std::string& name) -> sol::optional<Entity>
			{
				Entity e = scene->FindEntityByName(name);
				if (!e.IsValid())
					return sol::nullopt;
				return e;
			};
			table["FindEntityByID"] = [scene](int64_t id) -> sol::optional<Entity>
			{
				Entity e = scene->FindEntityByUUID(FromLuaID(id));
				if (!e.IsValid())
					return sol::nullopt;
				return e;
			};
			table["GetEntityCount"] = [scene]() { return static_cast<int64_t>(scene->GetEntityCount()); };
		}

	}

	struct ScriptEngine::Impl
	{
		struct Instance
		{
			sol::table Callbacks;
			bool Faulted = false;
		};

		Scope<sol::state> Lua;
		Scene* ScenePtr = nullptr;
		std::unordered_map<UUID, Instance> Instances;

		void Reset(Scene* scene, PhysicsWorld* physics)
		{
			ScenePtr = scene;
			Lua = CreateScope<sol::state>();
			Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8, sol::lib::coroutine);
			BindMath(*Lua);
			BindTransform(*Lua);
			BindEntity(*Lua);
			BindLog(*Lua);
			BindScene(*Lua, scene);
			BindPhysics(*Lua, physics);
		}

		void Fault(const Entity& entity, Instance& instance, const char* what, const sol::error& error)
		{
			LM_ERROR("Script error in entity '{}' during {}: {}", entity.GetName(), what, error.what());
			instance.Faulted = true;
		}

		void Call(Entity entity, Instance& instance, const char* callback, std::optional<float> deltaTime)
		{
			if (instance.Faulted)
				return;

			sol::object function = instance.Callbacks[callback];
			if (function.get_type() != sol::type::function)
				return;

			sol::protected_function pf = function;
			sol::protected_function_result result = deltaTime ? pf(entity, *deltaTime) : pf(entity);
			if (!result.valid())
				Fault(entity, instance, callback, result);
		}

		void CreateInstance(Entity entity)
		{
			Instance instance;
			const std::string& source = entity.GetComponent<ScriptComponent>().Source;

			sol::protected_function_result result = Lua->safe_script(source, sol::script_pass_on_error, entity.GetName());
			if (!result.valid())
			{
				Fault(entity, instance, "load", result);
			}
			else if (result.get_type() != sol::type::table)
			{
				LM_ERROR("Script for entity '{}' must return a table of callbacks", entity.GetName());
				instance.Faulted = true;
			}
			else
			{
				instance.Callbacks = result;
			}

			auto [it, inserted] = Instances.emplace(entity.GetUUID(), std::move(instance));
			Call(entity, it->second, "OnCreate", std::nullopt);
		}

		void OnScriptDestroyed(entt::registry& registry, entt::entity handle)
		{
			UUID id = registry.get<IDComponent>(handle).ID;
			auto it = Instances.find(id);
			if (it == Instances.end())
				return;

			Call(Entity(handle, ScenePtr), it->second, "OnDestroy", std::nullopt);
			Instances.erase(it);
		}
	};

	ScriptEngine::ScriptEngine() : m_Impl(CreateScope<Impl>()) {}

	ScriptEngine::~ScriptEngine()
	{
		if (IsRunning())
			Stop();
	}

	void ScriptEngine::Start(Scene& scene, PhysicsWorld* physics)
	{
		LM_ASSERT(!IsRunning(), "ScriptEngine already started");
		m_Scene = &scene;
		m_Impl->Reset(&scene, physics);
		scene.GetRegistry().on_destroy<ScriptComponent>().connect<&Impl::OnScriptDestroyed>(*m_Impl);
	}

	void ScriptEngine::Stop()
	{
		if (!IsRunning())
			return;

		m_Scene->GetRegistry().on_destroy<ScriptComponent>().disconnect<&Impl::OnScriptDestroyed>(*m_Impl);

		std::vector<UUID> ids;
		ids.reserve(m_Impl->Instances.size());
		for (auto& [id, instance] : m_Impl->Instances)
			ids.push_back(id);

		for (UUID id : ids)
		{
			auto it = m_Impl->Instances.find(id);
			Entity entity = m_Scene->FindEntityByUUID(id);
			if (it != m_Impl->Instances.end() && entity.IsValid())
				m_Impl->Call(entity, it->second, "OnDestroy", std::nullopt);
		}

		m_Impl->Instances.clear();
		m_Impl->Lua.reset();
		m_Impl->ScenePtr = nullptr;
		m_Scene = nullptr;
	}

	void ScriptEngine::Update(float deltaTime)
	{
		LM_ASSERT(IsRunning(), "ScriptEngine not started");

		// Snapshot entities first: scripts may create or destroy entities while we iterate.
		std::vector<UUID> pending;
		for (auto [handle, id, script] : m_Scene->GetRegistry().view<IDComponent, ScriptComponent>().each())
		{
			if (!m_Impl->Instances.contains(id.ID))
				pending.push_back(id.ID);
		}

		for (UUID id : pending)
		{
			Entity entity = m_Scene->FindEntityByUUID(id);
			if (entity.IsValid() && entity.HasComponent<ScriptComponent>() && !m_Impl->Instances.contains(id))
				m_Impl->CreateInstance(entity);
		}

		std::vector<UUID> live;
		live.reserve(m_Impl->Instances.size());
		for (auto& [id, instance] : m_Impl->Instances)
			live.push_back(id);

		for (UUID id : live)
		{
			auto it = m_Impl->Instances.find(id);
			Entity entity = m_Scene->FindEntityByUUID(id);
			if (it != m_Impl->Instances.end() && entity.IsValid())
				m_Impl->Call(entity, it->second, "OnUpdate", deltaTime);
		}
	}

	size_t ScriptEngine::GetInstanceCount() const
	{
		return m_Impl->Instances.size();
	}

	bool ScriptEngine::Execute(std::string_view code, std::string* output, std::string* error)
	{
		if (!IsRunning())
		{
			if (error)
				*error = "script engine is not running";
			return false;
		}

		sol::protected_function_result result = m_Impl->Lua->safe_script(code, sol::script_pass_on_error);
		if (!result.valid())
		{
			sol::error failure = result;
			LM_ERROR("Lua error: {}", failure.what());
			if (error)
				*error = failure.what();
			return false;
		}

		if (output && result.return_count() > 0)
		{
			sol::protected_function tostring = (*m_Impl->Lua)["tostring"];
			sol::object value = result.get<sol::object>();
			*output = tostring(value).get<std::string>();
		}
		return true;
	}

}
