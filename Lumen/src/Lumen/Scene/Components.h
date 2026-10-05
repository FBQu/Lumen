#pragma once

#include "Lumen/Core/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <string>

namespace Lumen {

	struct IDComponent
	{
		UUID ID;
	};

	struct TagComponent
	{
		std::string Tag;
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // Euler angles, radians (pitch, yaw, roll)
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		glm::mat4 GetTransform() const
		{
			return glm::translate(glm::mat4(1.0f), Translation)
				* glm::toMat4(glm::quat(Rotation))
				* glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	// Attaches a Lua script to an entity. The script is a chunk that returns a table of callbacks:
	// OnCreate(entity), OnUpdate(entity, dt), OnDestroy(entity). All callbacks are optional.
	struct ScriptComponent
	{
		std::string Source;
	};

	enum class BodyType { Static, Kinematic, Dynamic };

	// Makes an entity a simulated body. Requires a ColliderComponent on the same entity.
	// Body properties are applied when the body is created; changing them afterwards has no effect
	// until the physics world is restarted.
	struct RigidbodyComponent
	{
		BodyType Type = BodyType::Dynamic;
		float Mass = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float GravityScale = 1.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		bool FixedRotation = false;
	};

	enum class ColliderShape { Box, Sphere, Capsule };

	// Collision shape. An entity with a collider but no RigidbodyComponent is a static collider.
	// Shape dimensions are multiplied by the entity's scale when the body is created.
	struct ColliderComponent
	{
		ColliderShape Shape = ColliderShape::Box;
		glm::vec3 HalfExtents = { 0.5f, 0.5f, 0.5f }; // Box
		float Radius = 0.5f;                          // Sphere and Capsule
		float HalfHeight = 0.5f;                      // Capsule: half the height of the cylindrical part
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };      // Shape offset from the entity origin
	};

}
