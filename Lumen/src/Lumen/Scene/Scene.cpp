#include "Lumen/Scene/Scene.h"
#include "Lumen/Scene/Entity.h"

#include <vector>

namespace Lumen {

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		LM_ASSERT(uuid.IsValid(), "Entity UUID must be valid");
		LM_ASSERT(!m_EntityMap.contains(uuid), "Duplicate entity UUID");

		Entity entity(m_Registry.create(), this);
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
		entity.AddComponent<TransformComponent>();
		m_EntityMap[uuid] = entity.GetHandle();
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity.IsValid() || entity.m_Scene != this)
			return;

		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity.GetHandle());
	}

	void Scene::Clear()
	{
		std::vector<entt::entity> handles;
		for (auto [handle, id] : m_Registry.view<IDComponent>().each())
			handles.push_back(handle);
		for (entt::entity handle : handles)
			DestroyEntity(Entity(handle, this));
	}

	Entity Scene::FindEntityByUUID(UUID uuid)
	{
		auto it = m_EntityMap.find(uuid);
		return it != m_EntityMap.end() ? Entity(it->second, this) : Entity{};
	}

	Entity Scene::FindEntityByName(const std::string& name)
	{
		for (auto [handle, tag] : m_Registry.view<TagComponent>().each())
		{
			if (tag.Tag == name)
				return Entity(handle, this);
		}
		return Entity{};
	}

	const std::string* Scene::FindPrefab(const std::string& name) const
	{
		auto it = m_Prefabs.find(name);
		return it != m_Prefabs.end() ? &it->second : nullptr;
	}

	std::vector<std::string> Scene::GetPrefabNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_Prefabs.size());
		for (const auto& [name, json] : m_Prefabs)
			names.push_back(name);
		return names;
	}

	size_t Scene::GetEntityCount() const
	{
		return m_EntityMap.size();
	}

}
