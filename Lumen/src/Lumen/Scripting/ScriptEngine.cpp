#include "Lumen/Scripting/ScriptEngine.h"

#include "Lumen/Core/Log.h"
#include "Lumen/Physics/PhysicsWorld.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"
#include "Lumen/Scene/SceneSerializer.h"

#include <sol/sol.hpp>

#include <nlohmann/json.hpp>

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

		template<typename T>
		T& RequireComponent(Entity& entity, const char* name)
		{
			if (!entity.IsValid())
				throw std::runtime_error("the entity no longer exists");
			if (!entity.HasComponent<T>())
				throw std::runtime_error(std::string("the entity has no ") + name + " component");
			return entity.GetComponent<T>();
		}

		PrimitiveType ParsePrimitive(const std::string& name)
		{
			if (name == "cube")   return PrimitiveType::Cube;
			if (name == "sphere") return PrimitiveType::Sphere;
			if (name == "plane")  return PrimitiveType::Plane;
			throw std::runtime_error("invalid primitive '" + name + "' (expected cube, sphere or plane)");
		}

		const char* PrimitiveName(PrimitiveType type)
		{
			switch (type)
			{
				case PrimitiveType::Cube:   return "cube";
				case PrimitiveType::Sphere: return "sphere";
				case PrimitiveType::Plane:  return "plane";
			}
			return "cube";
		}

		void BindRenderComponents(sol::state& lua)
		{
			lua.new_usertype<glm::vec4>("vec4",
				sol::constructors<glm::vec4(), glm::vec4(float, float, float, float)>(),
				"x", sol::property([](const glm::vec4& v) { return v.x; }, [](glm::vec4& v, float x) { v.x = x; }),
				"y", sol::property([](const glm::vec4& v) { return v.y; }, [](glm::vec4& v, float y) { v.y = y; }),
				"z", sol::property([](const glm::vec4& v) { return v.z; }, [](glm::vec4& v, float z) { v.z = z; }),
				"w", sol::property([](const glm::vec4& v) { return v.w; }, [](glm::vec4& v, float w) { v.w = w; }),
				sol::meta_function::to_string, [](const glm::vec4& v) { return std::format("vec4({}, {}, {}, {})", v.x, v.y, v.z, v.w); });

			lua.new_usertype<MeshRendererComponent>("MeshRendererComponent", sol::no_constructor,
				"Primitive", sol::property([](const MeshRendererComponent& m) { return std::string(PrimitiveName(m.Primitive)); },
				                           [](MeshRendererComponent& m, const std::string& name) { m.Primitive = ParsePrimitive(name); }),
				"BaseColor", sol::property([](MeshRendererComponent& m) -> glm::vec4& { return m.Material.BaseColor; },
				                           [](MeshRendererComponent& m, const glm::vec4& v) { m.Material.BaseColor = v; }),
				"Metallic", sol::property([](const MeshRendererComponent& m) { return m.Material.Metallic; },
				                          [](MeshRendererComponent& m, float v) { m.Material.Metallic = v; }),
				"Roughness", sol::property([](const MeshRendererComponent& m) { return m.Material.Roughness; },
				                           [](MeshRendererComponent& m, float v) { m.Material.Roughness = v; }),
				"Emissive", sol::property([](MeshRendererComponent& m) -> glm::vec3& { return m.Material.Emissive; },
				                          [](MeshRendererComponent& m, const glm::vec3& v) { m.Material.Emissive = v; }),
				"CastShadows", &MeshRendererComponent::CastShadows);

			lua.new_usertype<CameraComponent>("CameraComponent", sol::no_constructor,
				"FovDegrees", sol::property([](const CameraComponent& c) { return glm::degrees(c.FovY); },
				                            [](CameraComponent& c, float degrees)
				                            {
				                            	if (!(degrees > 1.0f && degrees < 179.0f))
				                            		throw std::runtime_error("FovDegrees must be between 1 and 179");
				                            	c.FovY = glm::radians(degrees);
				                            }),
				"Near", &CameraComponent::Near,
				"Far", &CameraComponent::Far);

			lua.new_usertype<DirectionalLightComponent>("DirectionalLightComponent", sol::no_constructor,
				"Color", sol::property([](DirectionalLightComponent& l) -> glm::vec3& { return l.Color; },
				                       [](DirectionalLightComponent& l, const glm::vec3& v) { l.Color = v; }),
				"Intensity", &DirectionalLightComponent::Intensity);

			// Properties must be registered on the usertype itself (assigning through a plain table would store a function).
			sol::usertype<Entity> entity = lua["Entity"];
			entity["MeshRenderer"] = sol::property([](Entity& e) -> MeshRendererComponent& { return RequireComponent<MeshRendererComponent>(e, "MeshRenderer"); });
			entity["Camera"] = sol::property([](Entity& e) -> CameraComponent& { return RequireComponent<CameraComponent>(e, "Camera"); });
			entity["DirectionalLight"] = sol::property([](Entity& e) -> DirectionalLightComponent& { return RequireComponent<DirectionalLightComponent>(e, "DirectionalLight"); });
			entity["HasMeshRenderer"] = [](const Entity& e) { return e.HasComponent<MeshRendererComponent>(); };
			entity["HasCamera"] = [](const Entity& e) { return e.HasComponent<CameraComponent>(); };
			entity["HasDirectionalLight"] = [](const Entity& e) { return e.HasComponent<DirectionalLightComponent>(); };

			entity["AddMeshRenderer"] = [](Entity& e, sol::optional<sol::table> options)
			{
				MeshRendererComponent mr;
				if (options)
				{
					sol::table o = *options;
					if (sol::optional<std::string> primitive = o["Primitive"])
						mr.Primitive = ParsePrimitive(*primitive);
					mr.Material.BaseColor = o.get_or("BaseColor", mr.Material.BaseColor);
					mr.Material.Metallic = o.get_or("Metallic", mr.Material.Metallic);
					mr.Material.Roughness = o.get_or("Roughness", mr.Material.Roughness);
					mr.Material.Emissive = o.get_or("Emissive", mr.Material.Emissive);
					mr.CastShadows = o.get_or("CastShadows", mr.CastShadows);
				}
				e.AddOrReplaceComponent<MeshRendererComponent>(mr);
			};
			entity["AddCamera"] = [](Entity& e, sol::optional<sol::table> options)
			{
				CameraComponent camera;
				if (options)
				{
					sol::table o = *options;
					const float degrees = o.get_or("FovDegrees", glm::degrees(camera.FovY));
					if (!(degrees > 1.0f && degrees < 179.0f))
						throw std::runtime_error("FovDegrees must be between 1 and 179");
					camera.FovY = glm::radians(degrees);
					camera.Near = o.get_or("Near", camera.Near);
					camera.Far = o.get_or("Far", camera.Far);
					if (!(camera.Near > 0.0f) || !(camera.Far > camera.Near))
						throw std::runtime_error("a camera needs 0 < Near < Far");
				}
				e.AddOrReplaceComponent<CameraComponent>(camera);
			};
			entity["AddDirectionalLight"] = [](Entity& e, sol::optional<sol::table> options)
			{
				DirectionalLightComponent light;
				if (options)
				{
					sol::table o = *options;
					light.Color = o.get_or("Color", light.Color);
					light.Intensity = o.get_or("Intensity", light.Intensity);
					if (light.Intensity < 0.0f)
						throw std::runtime_error("light Intensity must not be negative");
				}
				e.AddOrReplaceComponent<DirectionalLightComponent>(light);
			};
			entity["SetScript"] = [](Entity& e, const std::string& source) { e.AddOrReplaceComponent<ScriptComponent>(source); };

			entity["RemoveComponent"] = [](Entity& e, const std::string& name)
			{
				if (!e.IsValid())
					throw std::runtime_error("the entity no longer exists");
				if (name == "MeshRenderer") { if (e.HasComponent<MeshRendererComponent>()) e.RemoveComponent<MeshRendererComponent>(); }
				else if (name == "Rigidbody") { if (e.HasComponent<RigidbodyComponent>()) e.RemoveComponent<RigidbodyComponent>(); }
				else if (name == "Collider") { if (e.HasComponent<ColliderComponent>()) e.RemoveComponent<ColliderComponent>(); }
				else if (name == "Camera") { if (e.HasComponent<CameraComponent>()) e.RemoveComponent<CameraComponent>(); }
				else if (name == "DirectionalLight") { if (e.HasComponent<DirectionalLightComponent>()) e.RemoveComponent<DirectionalLightComponent>(); }
				else if (name == "Script") { if (e.HasComponent<ScriptComponent>()) e.RemoveComponent<ScriptComponent>(); }
				else throw std::runtime_error("cannot remove component '" + name + "' (expected MeshRenderer, Rigidbody, Collider, Camera, DirectionalLight or Script)");
			};
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
			table["GetAllEntities"] = [scene](sol::this_state state)
			{
				sol::state_view view(state);
				sol::table result = view.create_table();
				int index = 1;
				for (auto [handle, id] : scene->GetRegistry().view<IDComponent>().each())
					result[index++] = Entity(handle, scene);
				return result;
			};
			table["HasPrefab"] = [scene](const std::string& name) { return scene->FindPrefab(name) != nullptr; };
			// Scene.Spawn(prefabName, { Name = "...", Translation = vec3, Rotation = vec3, Scale = vec3 })
			table["Spawn"] = [scene](const std::string& name, sol::optional<sol::table> options) -> Entity
			{
				nlohmann::json overrides = nlohmann::json::object();
				if (options)
				{
					sol::table o = *options;
					auto vec = [](const glm::vec3& v) { return nlohmann::json::array({ v.x, v.y, v.z }); };
					// Present-but-wrongly-typed options are errors; silently ignoring them would hide script bugs.
					auto readVec = [&o](const char* key) -> std::optional<glm::vec3>
					{
						sol::object value = o[key];
						if (value.get_type() == sol::type::lua_nil)
							return std::nullopt;
						if (!value.is<glm::vec3>())
							throw std::runtime_error(std::string("Scene.Spawn option '") + key + "' must be a vec3");
						return value.as<glm::vec3>();
					};
					sol::object nameValue = o["Name"];
					if (nameValue.get_type() != sol::type::lua_nil)
					{
						if (nameValue.get_type() != sol::type::string)
							throw std::runtime_error("Scene.Spawn option 'Name' must be a string");
						overrides["name"] = nameValue.as<std::string>();
					}
					nlohmann::json transform = nlohmann::json::object();
					if (auto t = readVec("Translation")) transform["translation"] = vec(*t);
					if (auto r = readVec("Rotation")) transform["rotation"] = vec(*r);
					if (auto sc = readVec("Scale")) transform["scale"] = vec(*sc);
					if (!transform.empty())
						overrides["transform"] = transform;
				}
				std::string error;
				Entity entity = SceneSerializer::SpawnPrefab(*scene, name, overrides.dump(), &error);
				if (!entity.IsValid())
					throw std::runtime_error("Scene.Spawn failed: " + error);
				return entity;
			};
		}

	}

	struct ScriptEngine::Impl
	{
		struct Instance
		{
			sol::table Callbacks;
			bool Faulted = false;
		};

		static constexpr int s_HookInterval = 1000; // the hook fires every this many VM instructions

		Scope<sol::state> Lua;
		uint64_t InstructionLimit = 5'000'000;
		uint64_t InstructionsUsed = 0;
		Scene* ScenePtr = nullptr;
		std::unordered_map<UUID, Instance> Instances;

		static void InstructionHook(lua_State* L, lua_Debug*)
		{
			Impl* impl = *static_cast<Impl**>(lua_getextraspace(L)); // set in Reset; inherited by coroutines
			impl->InstructionsUsed += s_HookInterval;
			if (impl->InstructionsUsed > impl->InstructionLimit)
				luaL_error(L, "script exceeded the instruction limit");
		}

		void Reset(Scene* scene, PhysicsWorld* physics)
		{
			ScenePtr = scene;
			Lua = CreateScope<sol::state>();
			Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8, sol::lib::coroutine);
			BindMath(*Lua);
			BindTransform(*Lua);
			BindEntity(*Lua);
			BindRenderComponents(*Lua);
			BindLog(*Lua);
			BindScene(*Lua, scene);
			BindPhysics(*Lua, physics);

			*static_cast<Impl**>(lua_getextraspace(Lua->lua_state())) = this;
			lua_sethook(Lua->lua_state(), &InstructionHook, LUA_MASKCOUNT, s_HookInterval);
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

			InstructionsUsed = 0;
			sol::protected_function pf = function;
			sol::protected_function_result result = deltaTime ? pf(entity, *deltaTime) : pf(entity);
			if (!result.valid())
				Fault(entity, instance, callback, result);
		}

		void CreateInstance(Entity entity)
		{
			Instance instance;
			const std::string& source = entity.GetComponent<ScriptComponent>().Source;

			InstructionsUsed = 0;
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

	void ScriptEngine::SetInstructionLimit(uint64_t limit) { m_Impl->InstructionLimit = limit; }
	uint64_t ScriptEngine::GetInstructionLimit() const { return m_Impl->InstructionLimit; }

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

		m_Impl->InstructionsUsed = 0;
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
