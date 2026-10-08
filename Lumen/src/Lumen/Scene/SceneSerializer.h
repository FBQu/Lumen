#pragma once

#include <string>
#include <string_view>

namespace Lumen {

	class Scene;
	class Entity;

	// JSON scene format (version 1). UUIDs are stored as decimal strings so 64-bit values survive any JSON parser.
	class SceneSerializer
	{
	public:
		static constexpr int s_FormatVersion = 1;

		static std::string Serialize(const Scene& scene);
		static std::string SerializeEntity(Scene& scene, Entity entity);

		// Adds the entities described by `json` to `scene`. All-or-nothing: the input is fully validated first,
		// so on failure the scene is untouched, false is returned and `error` (if given) describes the problem.
		static bool Deserialize(Scene& scene, std::string_view json, std::string* error = nullptr);

		// Stores a copy of `source` as the prefab `name` (replacing any prefab with that name). Names are 1..128 characters.
		static bool CreatePrefab(Scene& scene, const std::string& name, Entity source, std::string* error = nullptr);

		// Creates a new entity (with a fresh id) from a prefab. `overridesJson` is an optional JSON merge patch applied to
		// the new entity, e.g. {"name": "Bullet 3", "transform": {"translation": [0, 1, 0]}}. All-or-nothing.
		// Returns an invalid Entity and sets `error` on failure.
		static Entity SpawnPrefab(Scene& scene, const std::string& name, std::string_view overridesJson = {}, std::string* error = nullptr);

		// Applies a JSON merge patch (RFC 7386) to one entity in place. Objects are merged key by key, `null`
		// removes a component or resets a value to its default, and the entity id cannot change. All-or-nothing.
		static bool PatchEntity(Scene& scene, Entity entity, std::string_view patchJson, std::string* error = nullptr);
	};

}
