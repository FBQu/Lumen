#include "Lumen/Renderer/Mesh.h"

#include "Lumen/Core/Log.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Lumen {

	namespace {

		template<int N>
		bool IsFinite(const glm::vec<N, float>& v)
		{
			for (int i = 0; i < N; i++)
				if (!std::isfinite(v[i]))
					return false;
			return true;
		}

	}

	void MeshData::ComputeNormals()
	{
		std::vector<glm::vec3> sums(Vertices.size(), glm::vec3(0.0f));
		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			const uint32_t a = Indices[i], b = Indices[i + 1], c = Indices[i + 2];
			if (a >= Vertices.size() || b >= Vertices.size() || c >= Vertices.size())
				continue;
			// The cross product's length is twice the triangle area, which gives area weighting for free.
			const glm::vec3 n = glm::cross(Vertices[b].Position - Vertices[a].Position, Vertices[c].Position - Vertices[a].Position);
			sums[a] += n;
			sums[b] += n;
			sums[c] += n;
		}
		for (size_t i = 0; i < Vertices.size(); i++)
			Vertices[i].Normal = glm::length(sums[i]) > 1e-12f ? glm::normalize(sums[i]) : glm::vec3(0.0f, 1.0f, 0.0f);
	}

	void MeshData::ComputeTangents()
	{
		std::vector<glm::vec3> tangents(Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangents(Vertices.size(), glm::vec3(0.0f));

		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			const uint32_t ia = Indices[i], ib = Indices[i + 1], ic = Indices[i + 2];
			if (ia >= Vertices.size() || ib >= Vertices.size() || ic >= Vertices.size())
				continue;
			const Vertex& a = Vertices[ia];
			const Vertex& b = Vertices[ib];
			const Vertex& c = Vertices[ic];

			const glm::vec3 e1 = b.Position - a.Position, e2 = c.Position - a.Position;
			const glm::vec2 d1 = b.UV - a.UV, d2 = c.UV - a.UV;
			const float det = d1.x * d2.y - d2.x * d1.y;
			if (std::abs(det) < 1e-12f)
				continue; // degenerate UV mapping
			const float inv = 1.0f / det;
			const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * inv;
			const glm::vec3 bt = (e2 * d1.x - e1 * d2.x) * inv;
			for (uint32_t index : { ia, ib, ic })
			{
				tangents[index] += t;
				bitangents[index] += bt;
			}
		}

		for (size_t i = 0; i < Vertices.size(); i++)
		{
			const glm::vec3 n = Vertices[i].Normal;
			glm::vec3 t = tangents[i] - n * glm::dot(n, tangents[i]); // Gram-Schmidt
			if (glm::length(t) < 1e-8f)
			{
				// No UV information: pick any direction perpendicular to the normal.
				t = std::abs(n.y) < 0.99f ? glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), n) : glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), n);
			}
			t = glm::normalize(t);
			const float handedness = glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
			Vertices[i].Tangent = glm::vec4(t, handedness);
		}
	}

	bool MeshData::IsValid() const
	{
		if (Indices.size() % 3 != 0)
			return false;
		for (uint32_t index : Indices)
			if (index >= Vertices.size())
				return false;
		for (const Vertex& v : Vertices)
		{
			if (!IsFinite(v.Position) || !IsFinite(v.Normal) || !IsFinite(v.UV) || !IsFinite(v.Tangent))
				return false;
		}
		return true;
	}

	namespace MeshGenerator {

		MeshData CreateCube()
		{
			struct Face { glm::vec3 Normal, Right, Up; };
			// Right x Up == Normal, so the quad (-r-u, +r-u, +r+u, -r+u) is counter-clockwise seen from outside.
			const Face faces[6] = {
				{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },   { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
				{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },  { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
				{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },  { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
			};

			MeshData mesh;
			for (const Face& f : faces)
			{
				const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
				const glm::vec3 center = f.Normal * 0.5f;
				mesh.Vertices.push_back({ center - f.Right * 0.5f - f.Up * 0.5f, f.Normal, { 0, 1 } });
				mesh.Vertices.push_back({ center + f.Right * 0.5f - f.Up * 0.5f, f.Normal, { 1, 1 } });
				mesh.Vertices.push_back({ center + f.Right * 0.5f + f.Up * 0.5f, f.Normal, { 1, 0 } });
				mesh.Vertices.push_back({ center - f.Right * 0.5f + f.Up * 0.5f, f.Normal, { 0, 0 } });
				for (uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
					mesh.Indices.push_back(base + i);
			}
			mesh.ComputeTangents();
			return mesh;
		}

		MeshData CreateSphere(uint32_t segments, uint32_t rings)
		{
			segments = std::max(segments, 3u);
			rings = std::max(rings, 2u);

			MeshData mesh;
			for (uint32_t ring = 0; ring <= rings; ring++)
			{
				const float v = static_cast<float>(ring) / static_cast<float>(rings);
				const float phi = v * glm::pi<float>(); // 0 at the north pole
				for (uint32_t seg = 0; seg <= segments; seg++)
				{
					const float u = static_cast<float>(seg) / static_cast<float>(segments);
					const float theta = u * glm::two_pi<float>();
					const glm::vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
					mesh.Vertices.push_back({ normal * 0.5f, normal, { u, v } });
				}
			}

			const uint32_t stride = segments + 1;
			for (uint32_t ring = 0; ring < rings; ring++)
			{
				for (uint32_t seg = 0; seg < segments; seg++)
				{
					const uint32_t a = ring * stride + seg;
					const uint32_t b = a + stride;
					// Skip the degenerate triangle at each pole.
					if (ring != 0)
						for (uint32_t i : { a, a + 1, b })
							mesh.Indices.push_back(i);
					if (ring != rings - 1)
						for (uint32_t i : { a + 1, b + 1, b })
							mesh.Indices.push_back(i);
				}
			}
			mesh.ComputeTangents();
			return mesh;
		}

		MeshData CreatePlane()
		{
			MeshData mesh;
			const glm::vec3 up(0, 1, 0);
			mesh.Vertices = { { { -0.5f, 0, 0.5f }, up, { 0, 1 } }, { { 0.5f, 0, 0.5f }, up, { 1, 1 } },
			                  { { 0.5f, 0, -0.5f }, up, { 1, 0 } }, { { -0.5f, 0, -0.5f }, up, { 0, 0 } } };
			mesh.Indices = { 0, 1, 2, 0, 2, 3 };
			mesh.ComputeTangents();
			return mesh;
		}

	}

}
