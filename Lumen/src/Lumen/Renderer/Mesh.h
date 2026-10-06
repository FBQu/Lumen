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
	};

	// CPU-side triangle mesh. Triangles are counter-clockwise when viewed from outside (the side the normal faces).
	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;

		size_t GetTriangleCount() const { return Indices.size() / 3; }
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
