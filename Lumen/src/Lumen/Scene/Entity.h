#pragma once

#include "Lumen/Core/Log.h"
#include "Lumen/Scene/Scene.h"

#include <entt/entt.hpp>

#include <utility>

namespace Lumen {

	// Lightweight handle to an entity inside a Scene. Cheap to copy; does not own the entity.
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene) : m_Handle(handle), m_Scene(scene) {}

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			LM_ASSERT(!HasComponent<T>(), "Entity already has component");
			return m_Scene->m_Registry.emplace<T>(m_Handle, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			return m_Scene->m_Registry.emplace_or_replace<T>(m_Handle, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent()
		{
			LM_ASSERT(HasComponent<T>(), "Entity does not have component");
			return m_Scene->m_Registry.get<T>(m_Handle);
		}

		template<typename T>
		const T& GetComponent() const
		{
			LM_ASSERT(HasComponent<T>(), "Entity does not have component");
			return m_Scene->m_Registry.get<T>(m_Handle);
		}

		template<typename T>
		bool HasComponent() const
		{
			return IsValid() && m_Scene->m_Registry.all_of<T>(m_Handle);
		}

		template<typename T>
		void RemoveComponent()
		{
			LM_ASSERT(HasComponent<T>(), "Entity does not have component");
			m_Scene->m_Registry.remove<T>(m_Handle);
		}

		bool IsValid() const { return m_Scene != nullptr && m_Scene->m_Registry.valid(m_Handle); }
		explicit operator bool() const { return IsValid(); }

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		const std::string& GetName() const { return GetComponent<TagComponent>().Tag; }

		bool operator==(const Entity& other) const { return m_Handle == other.m_Handle && m_Scene == other.m_Scene; }
		entt::entity GetHandle() const { return m_Handle; }
	private:
		entt::entity m_Handle = entt::null;
		Scene* m_Scene = nullptr;

		friend class Scene;
	};

}
