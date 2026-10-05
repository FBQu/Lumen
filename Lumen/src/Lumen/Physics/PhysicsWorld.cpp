#include "Lumen/Physics/PhysicsWorld.h"

#include "Lumen/Core/Log.h"
#include "Lumen/Scene/Scene.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/component_wise.hpp>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Lumen {

	namespace {

		void JoltTrace(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			LM_ERROR("Jolt: {}", buffer);
		}

		// Jolt's global factory/type registration is reference counted so several worlds can coexist.
		class JoltRuntime
		{
		public:
			static void Acquire()
			{
				std::lock_guard lock(s_Mutex);
				if (s_RefCount++ == 0)
				{
					JPH::RegisterDefaultAllocator();
					JPH::Trace = &JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
					JPH::AssertFailed = [](const char* expression, const char* message, const char* file, JPH::uint line)
					{
						LM_FATAL("Jolt assertion failed: {} ({}) at {}:{}", expression, message != nullptr ? message : "", file, line);
						return true; // trigger a breakpoint
					};
#endif
					JPH::Factory::sInstance = new JPH::Factory();
					JPH::RegisterTypes();
				}
			}

			static void Release()
			{
				std::lock_guard lock(s_Mutex);
				if (--s_RefCount == 0)
				{
					JPH::UnregisterTypes();
					delete JPH::Factory::sInstance;
					JPH::Factory::sInstance = nullptr;
				}
			}
		private:
			static inline std::mutex s_Mutex;
			static inline int s_RefCount = 0;
		};

		namespace Layers
		{
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::uint Count = 2;
		}

		namespace BroadPhaseLayers
		{
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr JPH::uint Count = 2;
		}

		class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::NonMoving ? "NON_MOVING" : "MOVING";
			}
#endif
		};

		class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				if (layer == Layers::NonMoving)
					return broadPhaseLayer == BroadPhaseLayers::Moving;
				return true;
			}
		};

		class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				return a == Layers::Moving || b == Layers::Moving; // static-vs-static never collides
			}
		};

		JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		glm::vec3 FromJolt(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::quat FromJolt(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		JPH::EMotionType ToJolt(BodyType type)
		{
			switch (type)
			{
				case BodyType::Static:    return JPH::EMotionType::Static;
				case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
				case BodyType::Dynamic:   return JPH::EMotionType::Dynamic;
			}
			return JPH::EMotionType::Static;
		}

		constexpr float s_MinDimension = 0.001f;

		// Returns a null ref if the collider dimensions are invalid.
		JPH::RefConst<JPH::Shape> CreateShape(const ColliderComponent& collider, const glm::vec3& scale)
		{
			const glm::vec3 absScale = glm::abs(scale);
			JPH::RefConst<JPH::Shape> shape;

			switch (collider.Shape)
			{
				case ColliderShape::Box:
				{
					const glm::vec3 half = glm::abs(collider.HalfExtents) * absScale;
					if (glm::compMin(half) < s_MinDimension)
						return nullptr;
					const float convexRadius = std::min(0.05f, glm::compMin(half));
					shape = new JPH::BoxShape(ToJolt(half), convexRadius);
					break;
				}
				case ColliderShape::Sphere:
				{
					const float radius = std::abs(collider.Radius) * glm::compMax(absScale);
					if (radius < s_MinDimension)
						return nullptr;
					shape = new JPH::SphereShape(radius);
					break;
				}
				case ColliderShape::Capsule:
				{
					const float radius = std::abs(collider.Radius) * std::max(absScale.x, absScale.z);
					const float halfHeight = std::abs(collider.HalfHeight) * absScale.y;
					if (radius < s_MinDimension || halfHeight < s_MinDimension)
						return nullptr;
					shape = new JPH::CapsuleShape(halfHeight, radius);
					break;
				}
			}

			if (shape != nullptr && collider.Offset != glm::vec3(0.0f))
				shape = new JPH::RotatedTranslatedShape(ToJolt(collider.Offset * absScale), JPH::Quat::sIdentity(), shape);
			return shape;
		}

	}

	struct PhysicsWorld::Impl
	{
		struct BodyRecord
		{
			JPH::BodyID ID;
			BodyType Type = BodyType::Static;
			glm::vec3 LastTranslation = { 0.0f, 0.0f, 0.0f };
			glm::vec3 LastRotation = { 0.0f, 0.0f, 0.0f };
		};

		BroadPhaseLayerInterface BPLayerInterface;
		ObjectVsBroadPhaseLayerFilter ObjectVsBPFilter;
		ObjectLayerPairFilter PairFilter;
		JPH::PhysicsSystem System;
		JPH::TempAllocatorImpl TempAllocator{ 32 * 1024 * 1024 };
		JPH::JobSystemThreadPool JobSystem;

		Scene* ScenePtr = nullptr;
		std::unordered_map<UUID, BodyRecord> Bodies;
		std::unordered_set<UUID> Rejected; // entities whose body creation failed; not retried every step
		float Accumulator = 0.0f;

		Impl()
			: JobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(std::max(1u, std::thread::hardware_concurrency())) - 1)
		{
			System.Init(10240, 0, 65536, 10240, BPLayerInterface, ObjectVsBPFilter, PairFilter);
		}

		JPH::BodyInterface& GetBodyInterface() { return System.GetBodyInterface(); }
		const JPH::BodyInterface& GetBodyInterface() const { return System.GetBodyInterface(); }

		void CreateBody(Entity entity)
		{
			const ColliderComponent& collider = entity.GetComponent<ColliderComponent>();
			const TransformComponent& transform = entity.GetComponent<TransformComponent>();

			JPH::RefConst<JPH::Shape> shape = CreateShape(collider, transform.Scale);
			if (shape == nullptr)
			{
				Rejected.insert(entity.GetUUID());
				LM_ERROR("Entity '{}' has an invalid collider size; no physics body created", entity.GetName());
				return;
			}

			RigidbodyComponent rigidbody; // defaults only matter for entities without a rigidbody
			rigidbody.Type = BodyType::Static;
			if (entity.HasComponent<RigidbodyComponent>())
				rigidbody = entity.GetComponent<RigidbodyComponent>();

			if (rigidbody.Type == BodyType::Dynamic && rigidbody.Mass <= 0.0f)
			{
				Rejected.insert(entity.GetUUID());
				LM_ERROR("Entity '{}' has a dynamic rigidbody with non-positive mass; no physics body created", entity.GetName());
				return;
			}

			const glm::quat rotation(transform.Rotation);
			const JPH::ObjectLayer layer = rigidbody.Type == BodyType::Static ? Layers::NonMoving : Layers::Moving;
			JPH::BodyCreationSettings settings(shape, ToJolt(transform.Translation), ToJolt(rotation), ToJolt(rigidbody.Type), layer);
			settings.mFriction = rigidbody.Friction;
			settings.mRestitution = rigidbody.Restitution;
			settings.mGravityFactor = rigidbody.GravityScale;
			settings.mLinearDamping = rigidbody.LinearDamping;
			settings.mAngularDamping = rigidbody.AngularDamping;
			settings.mUserData = static_cast<uint64_t>(entity.GetUUID());
			if (rigidbody.Type == BodyType::Dynamic)
			{
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = rigidbody.Mass;
				if (rigidbody.FixedRotation)
					settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
			}

			const JPH::EActivation activation = rigidbody.Type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
			const JPH::BodyID id = GetBodyInterface().CreateAndAddBody(settings, activation);
			if (id.IsInvalid())
			{
				Rejected.insert(entity.GetUUID());
				LM_ERROR("Physics body limit reached; entity '{}' has no body", entity.GetName());
				return;
			}

			Bodies[entity.GetUUID()] = BodyRecord{ id, rigidbody.Type, transform.Translation, transform.Rotation };
		}

		void DestroyBody(UUID uuid)
		{
			auto it = Bodies.find(uuid);
			if (it == Bodies.end())
				return;

			JPH::BodyInterface& bodies = GetBodyInterface();
			bodies.RemoveBody(it->second.ID);
			bodies.DestroyBody(it->second.ID);
			Bodies.erase(it);
		}

		void OnColliderDestroyed(entt::registry& registry, entt::entity handle)
		{
			const UUID uuid = registry.get<IDComponent>(handle).ID;
			Rejected.erase(uuid);
			DestroyBody(uuid);
		}

		void CreateMissingBodies()
		{
			std::vector<Entity> missing;
			for (auto [handle, id, collider] : ScenePtr->GetRegistry().view<IDComponent, ColliderComponent>().each())
			{
				if (!Bodies.contains(id.ID) && !Rejected.contains(id.ID))
					missing.emplace_back(handle, ScenePtr);
			}
			for (Entity& entity : missing)
				CreateBody(entity);
		}

		// Pushes transform edits made outside the simulation into Jolt.
		void PushTransforms(float deltaTime)
		{
			JPH::BodyInterface& bodies = GetBodyInterface();
			for (auto& [uuid, record] : Bodies)
			{
				Entity entity = ScenePtr->FindEntityByUUID(uuid);
				const TransformComponent& transform = entity.GetComponent<TransformComponent>();
				const bool changed = transform.Translation != record.LastTranslation || transform.Rotation != record.LastRotation;
				const JPH::Vec3 position = ToJolt(transform.Translation);
				const JPH::Quat rotation = ToJolt(glm::quat(transform.Rotation));

				if (record.Type == BodyType::Kinematic)
				{
					bodies.MoveKinematic(record.ID, position, rotation, deltaTime);
				}
				else if (changed)
				{
					const JPH::EActivation activation = record.Type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
					bodies.SetPositionAndRotation(record.ID, position, rotation, activation);
				}
				record.LastTranslation = transform.Translation;
				record.LastRotation = transform.Rotation;
			}
		}

		// Writes simulated poses of dynamic bodies back to their transforms.
		void PullTransforms()
		{
			JPH::BodyInterface& bodies = GetBodyInterface();
			for (auto& [uuid, record] : Bodies)
			{
				if (record.Type != BodyType::Dynamic || !bodies.IsActive(record.ID))
					continue;

				JPH::RVec3 position;
				JPH::Quat rotation;
				bodies.GetPositionAndRotation(record.ID, position, rotation);

				TransformComponent& transform = ScenePtr->FindEntityByUUID(uuid).GetComponent<TransformComponent>();
				transform.Translation = FromJolt(position);
				transform.Rotation = glm::eulerAngles(FromJolt(rotation));
				record.LastTranslation = transform.Translation;
				record.LastRotation = transform.Rotation;
			}
		}

		const BodyRecord* FindDynamic(Entity entity) const
		{
			if (!entity.IsValid())
				return nullptr;
			auto it = Bodies.find(entity.GetUUID());
			return it != Bodies.end() && it->second.Type == BodyType::Dynamic ? &it->second : nullptr;
		}
	};

	PhysicsWorld::PhysicsWorld()
	{
		JoltRuntime::Acquire();
		m_Impl = CreateScope<Impl>();
	}

	PhysicsWorld::~PhysicsWorld()
	{
		if (IsRunning())
			Stop();
		m_Impl.reset(); // Jolt objects must be released before the runtime is torn down
		JoltRuntime::Release();
	}

	void PhysicsWorld::Start(Scene& scene)
	{
		LM_ASSERT(!IsRunning(), "PhysicsWorld already started");
		m_Scene = &scene;
		m_Impl->ScenePtr = &scene;
		m_Impl->Accumulator = 0.0f;
		scene.GetRegistry().on_destroy<ColliderComponent>().connect<&Impl::OnColliderDestroyed>(*m_Impl);
		m_Impl->CreateMissingBodies();
	}

	void PhysicsWorld::Stop()
	{
		if (!IsRunning())
			return;

		m_Scene->GetRegistry().on_destroy<ColliderComponent>().disconnect<&Impl::OnColliderDestroyed>(*m_Impl);

		std::vector<UUID> ids;
		for (auto& [uuid, record] : m_Impl->Bodies)
			ids.push_back(uuid);
		for (UUID uuid : ids)
			m_Impl->DestroyBody(uuid);

		m_Impl->Rejected.clear();
		m_Impl->ScenePtr = nullptr;
		m_Scene = nullptr;
	}

	int PhysicsWorld::Step(float deltaTime)
	{
		LM_ASSERT(IsRunning(), "PhysicsWorld not started");
		if (!(deltaTime > 0.0f))
			return 0;

		m_Impl->CreateMissingBodies();
		m_Impl->PushTransforms(s_FixedTimestep);

		m_Impl->Accumulator += deltaTime;
		int steps = 0;
		while (m_Impl->Accumulator >= s_FixedTimestep && steps < s_MaxSubsteps)
		{
			m_Impl->System.Update(s_FixedTimestep, 1, &m_Impl->TempAllocator, &m_Impl->JobSystem);
			m_Impl->Accumulator -= s_FixedTimestep;
			steps++;
		}
		if (steps == s_MaxSubsteps)
			m_Impl->Accumulator = 0.0f; // drop the backlog instead of spiraling

		m_Impl->PullTransforms();
		return steps;
	}

	size_t PhysicsWorld::GetBodyCount() const { return m_Impl->Bodies.size(); }

	void PhysicsWorld::SetGravity(const glm::vec3& gravity) { m_Impl->System.SetGravity(ToJolt(gravity)); }
	glm::vec3 PhysicsWorld::GetGravity() const { return FromJolt(m_Impl->System.GetGravity()); }

	bool PhysicsWorld::AddForce(Entity entity, const glm::vec3& force)
	{
		const auto* record = m_Impl->FindDynamic(entity);
		if (record == nullptr)
			return false;
		m_Impl->GetBodyInterface().AddForce(record->ID, ToJolt(force));
		return true;
	}

	bool PhysicsWorld::AddImpulse(Entity entity, const glm::vec3& impulse)
	{
		const auto* record = m_Impl->FindDynamic(entity);
		if (record == nullptr)
			return false;
		m_Impl->GetBodyInterface().AddImpulse(record->ID, ToJolt(impulse));
		return true;
	}

	bool PhysicsWorld::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
	{
		const auto* record = m_Impl->FindDynamic(entity);
		if (record == nullptr)
			return false;
		m_Impl->GetBodyInterface().SetLinearVelocity(record->ID, ToJolt(velocity));
		return true;
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(Entity entity) const
	{
		const auto* record = m_Impl->FindDynamic(entity);
		return record != nullptr ? FromJolt(m_Impl->GetBodyInterface().GetLinearVelocity(record->ID)) : glm::vec3(0.0f);
	}

	std::optional<RaycastHit> PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
	{
		if (!IsRunning() || !(maxDistance > 0.0f) || glm::length(direction) < 1e-6f)
			return std::nullopt;

		const glm::vec3 unit = glm::normalize(direction);
		const JPH::RRayCast ray(ToJolt(origin), ToJolt(unit * maxDistance));
		JPH::RayCastResult result;
		if (!m_Impl->System.GetNarrowPhaseQuery().CastRay(ray, result))
			return std::nullopt;

		RaycastHit hit;
		hit.Distance = result.mFraction * maxDistance;
		hit.Point = origin + unit * hit.Distance;

		JPH::BodyLockRead lock(m_Impl->System.GetBodyLockInterface(), result.mBodyID);
		if (!lock.Succeeded())
			return std::nullopt;

		const JPH::Body& body = lock.GetBody();
		hit.Normal = FromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ToJolt(hit.Point)));
		hit.HitEntity = m_Scene->FindEntityByUUID(UUID(body.GetUserData()));
		return hit;
	}

}
