#include "Lumen/Scene/Scene.h"
#include "Lumen/Scene/Entity.h"

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

	size_t Scene::GetEntityCount() const
	{
		return m_EntityMap.size();
	}

}
