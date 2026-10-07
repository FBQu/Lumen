#include "Lumen/Renderer/Shadow.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Lumen {

	void Bounds::Expand(const glm::vec3& point)
	{
		Min = glm::min(Min, point);
		Max = glm::max(Max, point);
	}

	void Bounds::Expand(const Bounds& other)
	{
		if (other.IsEmpty())
			return;
		Expand(other.Min);
		Expand(other.Max);
	}

	glm::vec3 Bounds::Corner(int index) const
	{
		return { (index & 1) ? Max.x : Min.x, (index & 2) ? Max.y : Min.y, (index & 4) ? Max.z : Min.z };
	}

	Bounds Bounds::Transformed(const glm::mat4& transform) const
	{
		Bounds result;
		if (IsEmpty())
			return result;
		for (int i = 0; i < 8; i++)
			result.Expand(glm::vec3(transform * glm::vec4(Corner(i), 1.0f)));
		return result;
	}

	bool ComputeDirectionalShadow(const glm::mat4& cameraWorld, float fovY, float aspect, float nearPlane, float farPlane,
	                              float shadowDistance, const glm::vec3& lightDirection, const Bounds& casters,
	                              uint32_t resolution, ShadowCascade& out)
	{
		if (glm::length(lightDirection) < 1e-6f || !(fovY > 0.0f) || !(aspect > 0.0f) || !(nearPlane > 0.0f)
			|| !(farPlane > nearPlane) || !(shadowDistance > nearPlane) || resolution < 16)
			return false;

		const glm::vec3 L = glm::normalize(lightDirection);
		const float sliceFar = std::min(farPlane, shadowDistance);

		// Bounding sphere of the frustum slice.
		const float tanHalf = std::tan(fovY * 0.5f);
		glm::vec3 corners[8];
		int count = 0;
		for (float d : { nearPlane, sliceFar })
		{
			const float h = tanHalf * d, w = h * aspect;
			for (float sx : { -1.0f, 1.0f })
				for (float sy : { -1.0f, 1.0f })
					corners[count++] = glm::vec3(cameraWorld * glm::vec4(sx * w, sy * h, -d, 1.0f));
		}
		glm::vec3 center(0.0f);
		for (const glm::vec3& c : corners)
			center += c;
		center /= 8.0f;
		float radius = 0.0f;
		for (const glm::vec3& c : corners)
			radius = std::max(radius, glm::length(c - center));
		radius = std::ceil(radius * 16.0f) / 16.0f; // quantize so tiny changes do not resize the map
		radius = std::max(radius, 0.01f);

		// Light-space basis and texel snapping of the center.
		const glm::vec3 up = std::abs(L.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 right = glm::normalize(glm::cross(L, up));
		const glm::vec3 trueUp = glm::cross(right, L);
		const float texel = 2.0f * radius / static_cast<float>(resolution);
		const float cx = std::floor(glm::dot(center, right) / texel) * texel;
		const float cy = std::floor(glm::dot(center, trueUp) / texel) * texel;
		const glm::vec3 snapped = right * cx + trueUp * cy + L * glm::dot(center, L);

		// Depth range along the light direction (t = 0 at the snapped center).
		float tMin = -radius, tMax = radius;
		if (!casters.IsEmpty())
		{
			for (int i = 0; i < 8; i++)
			{
				const float t = glm::dot(casters.Corner(i) - snapped, L);
				tMin = std::min(tMin, std::max(t, -4.0f * radius));
				tMax = std::max(tMax, std::min(t, 4.0f * radius));
			}
		}

		const glm::vec3 eye = snapped + L * tMin;
		out.View = glm::lookAtRH(eye, eye + L, trueUp);
		out.DepthRange = tMax - tMin;
		out.Proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, out.DepthRange);
		out.ViewProj = out.Proj * out.View;
		out.Radius = radius;
		out.TexelWorldSize = texel;
		return true;
	}

}
