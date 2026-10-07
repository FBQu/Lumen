#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Lumen {

	// Axis-aligned bounding box. A default-constructed box is empty.
	struct Bounds
	{
		glm::vec3 Min = { 1e30f, 1e30f, 1e30f };
		glm::vec3 Max = { -1e30f, -1e30f, -1e30f };

		bool IsEmpty() const { return Min.x > Max.x; }
		void Expand(const glm::vec3& point);
		void Expand(const Bounds& other);
		// Bounds of this box after an affine transform (the box of its 8 transformed corners).
		Bounds Transformed(const glm::mat4& transform) const;
		glm::vec3 Corner(int index) const; // index 0..7
	};

	// A single orthographic shadow cascade for a directional light.
	struct ShadowCascade
	{
		glm::mat4 View = glm::mat4(1.0f);
		glm::mat4 Proj = glm::mat4(1.0f);
		glm::mat4 ViewProj = glm::mat4(1.0f);
		float Radius = 1.0f;        // half width/height of the orthographic box in world units
		float DepthRange = 1.0f;    // far - near of the orthographic projection in world units
		float TexelWorldSize = 1.0f; // world size of one shadow texel
	};

	// Fits a stable shadow map to the part of the camera frustum between nearPlane and min(farPlane, shadowDistance).
	// - The box is a bounding sphere of that frustum slice, so its size does not change when the camera rotates.
	// - The box position snaps to whole texels, so shadows do not shimmer when the camera moves.
	// - The depth range is extended toward the light to include `casters` (limited to a few radii) so objects outside
	//   the view that cast into it are not clipped.
	// `cameraWorld` is the camera's world transform (looking down its -Z axis); `lightDirection` is the direction the
	// light travels. Returns false for degenerate input (zero light direction, non-positive sizes).
	bool ComputeDirectionalShadow(const glm::mat4& cameraWorld, float fovY, float aspect, float nearPlane, float farPlane,
	                              float shadowDistance, const glm::vec3& lightDirection, const Bounds& casters,
	                              uint32_t resolution, ShadowCascade& out);

}
