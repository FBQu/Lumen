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

}
