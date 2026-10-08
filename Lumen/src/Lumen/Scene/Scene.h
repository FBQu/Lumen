#pragma once

#include "Lumen/Core/UUID.h"
#include "Lumen/Scene/Components.h"

#include <entt/entt.hpp>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lumen {

	class Entity;

	class Scene
	{
	public:
		Scene() = default;
		~Scene() = default;
		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		Entity CreateEntity(const std::string& name = "Entity");
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
		void DestroyEntity(Entity entity);
		void Clear(); // destroys every entity

		Entity FindEntityByUUID(UUID uuid);
		Entity FindEntityByName(const std::string& name);

		size_t GetEntityCount() const;

		// Prefabs are named entity templates (entity JSON without an id) stored with the scene and saved with it.
		// Spawn them with SceneSerializer::SpawnPrefab.
		void SetPrefab(const std::string& name, std::string entityJson) { m_Prefabs[name] = std::move(entityJson); }
		const std::string* FindPrefab(const std::string& name) const;
		bool RemovePrefab(const std::string& name) { return m_Prefabs.erase(name) > 0; }
		std::vector<std::string> GetPrefabNames() const;
		void ClearPrefabs() { m_Prefabs.clear(); }

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }
	private:
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::map<std::string, std::string> m_Prefabs;

		friend class Entity;
	};

}
