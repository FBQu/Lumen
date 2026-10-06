#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Lumen {

	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 UV;
		glm::vec4 Tangent = { 1.0f, 0.0f, 0.0f, 1.0f }; // xyz direction of increasing U, w = bitangent handedness (+1 or -1)
	};

	// CPU-side triangle mesh. Triangles are counter-clockwise when viewed from outside (the side the normal faces).
	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;

		size_t GetTriangleCount() const { return Indices.size() / 3; }
		// Replaces vertex normals with area-weighted smooth normals computed from the triangles.
		void ComputeNormals();
		// Computes tangents from UV derivatives (needs valid normals). Vertices without a usable UV mapping get an
		// arbitrary tangent perpendicular to the normal.
		void ComputeTangents();
		// True if every index is in range, the index count is a multiple of 3 and all attributes are finite.
		bool IsValid() const;
	};

	namespace MeshGenerator
	{
		MeshData CreateCube();                                              // edge length 1, centered at the origin
		MeshData CreateSphere(uint32_t segments = 32, uint32_t rings = 16); // radius 0.5; segments >= 3, rings >= 2
		MeshData CreatePlane();                                             // 1x1 in XZ, normal +Y
	}

}
