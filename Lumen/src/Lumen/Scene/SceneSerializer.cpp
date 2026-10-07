#include "Lumen/Scene/SceneSerializer.h"

#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace Lumen {

	using Json = nlohmann::json;

	namespace {

		struct ParseError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		Json VecToJson(const glm::vec3& v) { return Json::array({ v.x, v.y, v.z }); }

		glm::vec3 VecFromJson(const Json& json, const char* what)
		{
			if (!json.is_array() || json.size() != 3)
				throw ParseError(std::string(what) + " must be an array of 3 numbers");
			glm::vec3 v;
			for (int i = 0; i < 3; i++)
			{
				if (!json[i].is_number())
					throw ParseError(std::string(what) + " must be an array of 3 numbers");
				v[i] = json[i].get<float>();
			}
			return v;
		}

		float NumberOr(const Json& json, const char* key, float fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_number())
				throw ParseError(std::string("'") + key + "' must be a number");
			return it->get<float>();
		}

		bool BoolOr(const Json& json, const char* key, bool fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_boolean())
				throw ParseError(std::string("'") + key + "' must be a boolean");
			return it->get<bool>();
		}

		glm::vec3 VecOr(const Json& json, const char* key, const glm::vec3& fallback)
		{
			auto it = json.find(key);
			return it == json.end() ? fallback : VecFromJson(*it, key);
		}

		const char* ToString(BodyType type)
		{
			switch (type)
			{
				case BodyType::Static:    return "static";
				case BodyType::Kinematic: return "kinematic";
				case BodyType::Dynamic:   return "dynamic";
			}
			return "static";
		}

		const char* ToString(ColliderShape shape)
		{
			switch (shape)
			{
				case ColliderShape::Box:     return "box";
				case ColliderShape::Sphere:  return "sphere";
				case ColliderShape::Capsule: return "capsule";
			}
			return "box";
		}

		BodyType BodyTypeFromString(const std::string& name)
		{
			if (name == "static")    return BodyType::Static;
			if (name == "kinematic") return BodyType::Kinematic;
			if (name == "dynamic")   return BodyType::Dynamic;
			throw ParseError("unknown body type '" + name + "'");
		}

		ColliderShape ShapeFromString(const std::string& name)
		{
			if (name == "box")     return ColliderShape::Box;
			if (name == "sphere")  return ColliderShape::Sphere;
			if (name == "capsule") return ColliderShape::Capsule;
			throw ParseError("unknown collider shape '" + name + "'");
		}

		std::string StringOr(const Json& json, const char* key, const std::string& fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_string())
				throw ParseError(std::string("'") + key + "' must be a string");
			return it->get<std::string>();
		}

		glm::vec4 Vec4OrDefault(const Json& json, const char* key, const glm::vec4& fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_array() || it->size() != 4)
				throw ParseError(std::string("'") + key + "' must be an array of 4 numbers");
			glm::vec4 v;
			for (int i = 0; i < 4; i++)
			{
				if (!(*it)[i].is_number())
					throw ParseError(std::string("'") + key + "' must be an array of 4 numbers");
				v[i] = (*it)[i].get<float>();
			}
			return v;
		}

		// Asset ids are decimal strings; "0" or a missing key means "none".
		UUID AssetIDOr(const Json& json, const char* key)
		{
			auto it = json.find(key);
			if (it == json.end())
				return UUID(0);
			if (!it->is_string())
				throw ParseError(std::string("'") + key + "' must be a decimal string");
			const std::string& text = it->get_ref<const std::string&>();
			uint64_t value = 0;
			auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (text.empty() || ec != std::errc() || end != text.data() + text.size())
				throw ParseError(std::string("'") + key + "' must be an unsigned 64-bit decimal string");
			return UUID(value);
		}

		const char* ToString(PrimitiveType type)
		{
			switch (type)
			{
				case PrimitiveType::Cube:   return "cube";
				case PrimitiveType::Sphere: return "sphere";
				case PrimitiveType::Plane:  return "plane";
			}
			return "cube";
		}

		PrimitiveType PrimitiveFromString(const std::string& name)
		{
			if (name == "cube")   return PrimitiveType::Cube;
			if (name == "sphere") return PrimitiveType::Sphere;
			if (name == "plane")  return PrimitiveType::Plane;
			throw ParseError("unknown primitive '" + name + "'");
		}

		Json Vec4ToJson(const glm::vec4& v) { return Json::array({ v.x, v.y, v.z, v.w }); }

		// Everything about one entity, validated and ready to be applied.
		struct EntityData
		{
			UUID ID;
			std::string Name;
			TransformComponent Transform;
			std::optional<RigidbodyComponent> Rigidbody;
			std::optional<ColliderComponent> Collider;
			std::optional<ScriptComponent> Script;
			std::optional<MeshRendererComponent> MeshRenderer;
			std::optional<CameraComponent> Camera;
			std::optional<DirectionalLightComponent> DirectionalLight;
		};

		UUID ParseID(const Json& json)
		{
			auto it = json.find("id");
			if (it == json.end() || !it->is_string())
				throw ParseError("entity 'id' must be a decimal string");

			const std::string& text = it->get_ref<const std::string&>();
			uint64_t value = 0;
			auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (text.empty() || ec != std::errc() || end != text.data() + text.size() || value == 0)
				throw ParseError("entity 'id' must be a non-zero unsigned 64-bit decimal string");
			return UUID(value);
		}

		EntityData ParseEntity(const Json& json)
		{
			if (!json.is_object())
				throw ParseError("each entity must be an object");

			EntityData data;
			data.ID = ParseID(json);
			data.Name = StringOr(json, "name", "Entity");
			if (data.Name.empty())
				data.Name = "Entity";

			if (auto it = json.find("transform"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'transform' must be an object");
				data.Transform.Translation = VecOr(*it, "translation", data.Transform.Translation);
				data.Transform.Rotation = VecOr(*it, "rotation", data.Transform.Rotation);
				data.Transform.Scale = VecOr(*it, "scale", data.Transform.Scale);
			}

			if (auto it = json.find("rigidbody"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'rigidbody' must be an object");
				RigidbodyComponent rb;
				rb.Type = BodyTypeFromString(StringOr(*it, "type", "dynamic"));
				rb.Mass = NumberOr(*it, "mass", rb.Mass);
				rb.Friction = NumberOr(*it, "friction", rb.Friction);
				rb.Restitution = NumberOr(*it, "restitution", rb.Restitution);
				rb.GravityScale = NumberOr(*it, "gravityScale", rb.GravityScale);
				rb.LinearDamping = NumberOr(*it, "linearDamping", rb.LinearDamping);
				rb.AngularDamping = NumberOr(*it, "angularDamping", rb.AngularDamping);
				rb.FixedRotation = BoolOr(*it, "fixedRotation", rb.FixedRotation);
				data.Rigidbody = rb;
			}

			if (auto it = json.find("collider"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'collider' must be an object");
				ColliderComponent c;
				c.Shape = ShapeFromString(StringOr(*it, "shape", "box"));
				c.HalfExtents = VecOr(*it, "halfExtents", c.HalfExtents);
				c.Radius = NumberOr(*it, "radius", c.Radius);
				c.HalfHeight = NumberOr(*it, "halfHeight", c.HalfHeight);
				c.Offset = VecOr(*it, "offset", c.Offset);
				data.Collider = c;
			}

			if (auto it = json.find("script"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'script' must be an object");
				data.Script = ScriptComponent{ StringOr(*it, "source", "") };
			}

			if (auto it = json.find("meshRenderer"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'meshRenderer' must be an object");
				MeshRendererComponent mr;
				mr.Primitive = PrimitiveFromString(StringOr(*it, "primitive", "cube"));
				if (auto m = it->find("material"); m != it->end())
				{
					if (!m->is_object())
						throw ParseError("'material' must be an object");
					mr.Material.BaseColor = Vec4OrDefault(*m, "baseColor", mr.Material.BaseColor);
					mr.Material.Metallic = NumberOr(*m, "metallic", mr.Material.Metallic);
					mr.Material.Roughness = NumberOr(*m, "roughness", mr.Material.Roughness);
					mr.Material.Emissive = VecOr(*m, "emissive", mr.Material.Emissive);
				}
				mr.MeshAsset = AssetIDOr(*it, "meshAsset");
				mr.MaterialAsset = AssetIDOr(*it, "materialAsset");
				mr.CastShadows = BoolOr(*it, "castShadows", mr.CastShadows);
				data.MeshRenderer = mr;
			}

			if (auto it = json.find("camera"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'camera' must be an object");
				CameraComponent camera;
				const float fovDegrees = NumberOr(*it, "fovDegrees", glm::degrees(camera.FovY));
				camera.Near = NumberOr(*it, "near", camera.Near);
				camera.Far = NumberOr(*it, "far", camera.Far);
				if (!(fovDegrees > 1.0f && fovDegrees < 179.0f))
					throw ParseError("camera 'fovDegrees' must be between 1 and 179");
				if (!(camera.Near > 0.0f) || !(camera.Far > camera.Near))
					throw ParseError("camera needs 0 < near < far");
				camera.FovY = glm::radians(fovDegrees);
				data.Camera = camera;
			}

			if (auto it = json.find("directionalLight"); it != json.end())
			{
				if (!it->is_object())
					throw ParseError("'directionalLight' must be an object");
				DirectionalLightComponent light;
				light.Color = VecOr(*it, "color", light.Color);
				light.Intensity = NumberOr(*it, "intensity", light.Intensity);
				if (light.Intensity < 0.0f)
					throw ParseError("light 'intensity' must not be negative");
				data.DirectionalLight = light;
			}

			return data;
		}

	}

	namespace {

		Json EntityToJson(const entt::registry& registry, entt::entity handle)
		{
			const auto& id = registry.get<IDComponent>(handle);
			const auto& tag = registry.get<TagComponent>(handle);
			const auto& transform = registry.get<TransformComponent>(handle);

			Json e;
			e["id"] = std::to_string(static_cast<uint64_t>(id.ID));
			e["name"] = tag.Tag;
			e["transform"] = { { "translation", VecToJson(transform.Translation) },
			                   { "rotation", VecToJson(transform.Rotation) },
			                   { "scale", VecToJson(transform.Scale) } };

			if (const auto* rb = registry.try_get<RigidbodyComponent>(handle))
			{
				e["rigidbody"] = { { "type", ToString(rb->Type) }, { "mass", rb->Mass }, { "friction", rb->Friction },
				                   { "restitution", rb->Restitution }, { "gravityScale", rb->GravityScale },
				                   { "linearDamping", rb->LinearDamping }, { "angularDamping", rb->AngularDamping },
				                   { "fixedRotation", rb->FixedRotation } };
			}
			if (const auto* c = registry.try_get<ColliderComponent>(handle))
			{
				e["collider"] = { { "shape", ToString(c->Shape) }, { "halfExtents", VecToJson(c->HalfExtents) },
				                  { "radius", c->Radius }, { "halfHeight", c->HalfHeight }, { "offset", VecToJson(c->Offset) } };
			}
			if (const auto* script = registry.try_get<ScriptComponent>(handle))
				e["script"] = { { "source", script->Source } };
			if (const auto* mr = registry.try_get<MeshRendererComponent>(handle))
			{
				Json material = { { "baseColor", Vec4ToJson(mr->Material.BaseColor) }, { "metallic", mr->Material.Metallic },
				                  { "roughness", mr->Material.Roughness }, { "emissive", VecToJson(mr->Material.Emissive) } };
				e["meshRenderer"] = { { "primitive", ToString(mr->Primitive) }, { "material", material },
				                      { "meshAsset", std::to_string(static_cast<uint64_t>(mr->MeshAsset)) },
				                      { "materialAsset", std::to_string(static_cast<uint64_t>(mr->MaterialAsset)) },
				                      { "castShadows", mr->CastShadows } };
			}
			if (const auto* camera = registry.try_get<CameraComponent>(handle))
				e["camera"] = { { "fovDegrees", glm::degrees(camera->FovY) }, { "near", camera->Near }, { "far", camera->Far } };
			if (const auto* light = registry.try_get<DirectionalLightComponent>(handle))
				e["directionalLight"] = { { "color", VecToJson(light->Color) }, { "intensity", light->Intensity } };
			return e;
		}

		// Brings an existing entity in line with validated data (components absent from the data are removed).
		void ApplyData(Entity entity, const EntityData& data)
		{
			entity.GetComponent<TagComponent>().Tag = data.Name;
			entity.GetComponent<TransformComponent>() = data.Transform;

			if (data.Rigidbody) entity.AddOrReplaceComponent<RigidbodyComponent>(*data.Rigidbody);
			else if (entity.HasComponent<RigidbodyComponent>()) entity.RemoveComponent<RigidbodyComponent>();

			if (data.Collider) entity.AddOrReplaceComponent<ColliderComponent>(*data.Collider);
			else if (entity.HasComponent<ColliderComponent>()) entity.RemoveComponent<ColliderComponent>();

			if (data.Script) entity.AddOrReplaceComponent<ScriptComponent>(*data.Script);
			else if (entity.HasComponent<ScriptComponent>()) entity.RemoveComponent<ScriptComponent>();

			if (data.MeshRenderer) entity.AddOrReplaceComponent<MeshRendererComponent>(*data.MeshRenderer);
			else if (entity.HasComponent<MeshRendererComponent>()) entity.RemoveComponent<MeshRendererComponent>();

			if (data.Camera) entity.AddOrReplaceComponent<CameraComponent>(*data.Camera);
			else if (entity.HasComponent<CameraComponent>()) entity.RemoveComponent<CameraComponent>();

			if (data.DirectionalLight) entity.AddOrReplaceComponent<DirectionalLightComponent>(*data.DirectionalLight);
			else if (entity.HasComponent<DirectionalLightComponent>()) entity.RemoveComponent<DirectionalLightComponent>();
		}

	}

	std::string SceneSerializer::Serialize(const Scene& scene)
	{
		Json entities = Json::array();

		for (auto [handle, id] : scene.GetRegistry().view<const IDComponent>().each())
			entities.push_back(EntityToJson(scene.GetRegistry(), handle));

		Json root;
		root["version"] = s_FormatVersion;
		root["entities"] = std::move(entities);
		return root.dump(2);
	}

	bool SceneSerializer::Deserialize(Scene& scene, std::string_view json, std::string* error)
	{
		auto fail = [error](const std::string& message)
		{
			if (error)
				*error = message;
			return false;
		};

		std::vector<EntityData> parsed;
		try
		{
			Json root = Json::parse(json);
			if (!root.is_object())
				return fail("scene root must be an object");

			auto version = root.find("version");
			if (version == root.end() || !version->is_number_integer())
				return fail("missing integer 'version'");
			if (version->get<int>() != s_FormatVersion)
				return fail("unsupported scene version " + std::to_string(version->get<int>()));

			auto entities = root.find("entities");
			if (entities == root.end() || !entities->is_array())
				return fail("missing 'entities' array");

			std::unordered_set<UUID> seen;
			for (const Json& entry : *entities)
			{
				EntityData data = ParseEntity(entry);
				if (!seen.insert(data.ID).second)
					return fail("duplicate entity id " + std::to_string(static_cast<uint64_t>(data.ID)));
				if (scene.FindEntityByUUID(data.ID).IsValid())
					return fail("entity id " + std::to_string(static_cast<uint64_t>(data.ID)) + " already exists in the scene");
				parsed.push_back(std::move(data));
			}
		}
		catch (const ParseError& e)
		{
			return fail(e.what());
		}
		catch (const Json::exception& e)
		{
			return fail(std::string("invalid JSON: ") + e.what());
		}

		for (EntityData& data : parsed)
		{
			ApplyData(scene.CreateEntityWithUUID(data.ID, data.Name), data);
		}
		return true;
	}

	std::string SceneSerializer::SerializeEntity(Scene& scene, Entity entity)
	{
		if (!entity.IsValid())
			return "null";
		return EntityToJson(scene.GetRegistry(), entity.GetHandle()).dump(2);
	}

	bool SceneSerializer::PatchEntity(Scene& scene, Entity entity, std::string_view patchJson, std::string* error)
	{
		auto fail = [error](const std::string& message)
		{
			if (error)
				*error = message;
			return false;
		};

		if (!entity.IsValid())
			return fail("entity does not exist");

		try
		{
			Json patch = Json::parse(patchJson);
			if (!patch.is_object())
				return fail("patch must be an object");

			Json merged = EntityToJson(scene.GetRegistry(), entity.GetHandle());
			if (auto id = patch.find("id"); id != patch.end() && *id != merged["id"])
				return fail("entity id cannot be changed");

			merged.merge_patch(patch);
			ApplyData(entity, ParseEntity(merged));
			return true;
		}
		catch (const ParseError& e)
		{
			return fail(e.what());
		}
		catch (const Json::exception& e)
		{
			return fail(std::string("invalid JSON: ") + e.what());
		}
	}

}
