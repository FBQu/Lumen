#pragma once

#include "Lumen/Core/Base.h"
#include "Lumen/Scene/Entity.h"

#include <glm/glm.hpp>

#include <optional>

namespace Lumen {

	struct RaycastHit
	{
		Entity HitEntity;
		glm::vec3 Point = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Normal = { 0.0f, 0.0f, 0.0f };
		float Distance = 0.0f;
	};

	// Simulates the bodies of one Scene (Jolt Physics). The Scene's TransformComponents are the
	// source of truth: writing a transform teleports the body, and simulation results are written back.
	class PhysicsWorld
	{
	public:
		static constexpr float s_FixedTimestep = 1.0f / 60.0f;
		static constexpr int s_MaxSubsteps = 8;

		PhysicsWorld();
		~PhysicsWorld();
		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		void Start(Scene& scene);
		void Stop();
		bool IsRunning() const { return m_Scene != nullptr; }

		// Advances the simulation in fixed steps. Returns the number of steps performed.
		int Step(float deltaTime);

		size_t GetBodyCount() const;
		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

		// The following only affect dynamic bodies; they return false (or zero) otherwise.
		// AddForce acts for the next simulation step only: call it every frame for a continuous force.
		bool AddForce(Entity entity, const glm::vec3& force);
		bool AddImpulse(Entity entity, const glm::vec3& impulse);
		bool SetLinearVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity(Entity entity) const;

		std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;
	private:
		struct Impl;
		Scope<Impl> m_Impl;
		Scene* m_Scene = nullptr;
	};

}
