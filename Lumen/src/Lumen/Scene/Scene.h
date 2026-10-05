#pragma once

#include "Lumen/Core/UUID.h"
#include "Lumen/Scene/Components.h"

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>

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

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }
	private:
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;

		friend class Entity;
	};

}
