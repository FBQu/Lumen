#include <doctest/doctest.h>

#include "Lumen/Renderer/Mesh.h"

#include <glm/gtx/component_wise.hpp>

#include <cmath>

using namespace Lumen;

namespace {

	// Every triangle's geometric normal must agree with its vertex normals (i.e. counter-clockwise from outside).
	void CheckWinding(const MeshData& mesh, float minAlignment = 0.5f)
	{
		for (size_t i = 0; i < mesh.Indices.size(); i += 3)
		{
			const Vertex& a = mesh.Vertices[mesh.Indices[i]];
			const Vertex& b = mesh.Vertices[mesh.Indices[i + 1]];
			const Vertex& c = mesh.Vertices[mesh.Indices[i + 2]];
			const glm::vec3 geometric = glm::cross(b.Position - a.Position, c.Position - a.Position);
			REQUIRE(glm::length(geometric) > 1e-6f); // no degenerate triangles
			REQUIRE(glm::dot(glm::normalize(geometric), (a.Normal + b.Normal + c.Normal) / 3.0f) > minAlignment);
		}
	}

	void CheckNormalsAreUnit(const MeshData& mesh)
	{
		for (const Vertex& v : mesh.Vertices)
			REQUIRE(glm::length(v.Normal) == doctest::Approx(1.0f).epsilon(1e-4));
	}

}

TEST_CASE("Cube mesh: 24 vertices, 12 triangles, outward counter-clockwise faces")
{
	MeshData cube = MeshGenerator::CreateCube();
	CHECK(cube.IsValid());
	CHECK(cube.Vertices.size() == 24);
	CHECK(cube.GetTriangleCount() == 12);
	CheckNormalsAreUnit(cube);
	CheckWinding(cube);

	for (const Vertex& v : cube.Vertices)
	{
		CHECK(glm::compMax(glm::abs(v.Position)) == doctest::Approx(0.5f));
		CHECK(glm::dot(v.Position, v.Normal) == doctest::Approx(0.5f)); // each vertex lies on its face plane
	}
}

TEST_CASE("Sphere mesh: radius 0.5, outward normals, no degenerate triangles")
{
	MeshData sphere = MeshGenerator::CreateSphere(24, 12);
	CHECK(sphere.IsValid());
	CHECK(sphere.Vertices.size() == 25u * 13u);
	CHECK(sphere.GetTriangleCount() == 24u * (12u - 1u) * 2u); // pole rows contribute one triangle per segment
	CheckNormalsAreUnit(sphere);
	CheckWinding(sphere);

	for (const Vertex& v : sphere.Vertices)
	{
		CHECK(glm::length(v.Position) == doctest::Approx(0.5f).epsilon(1e-4));
		CHECK(glm::dot(glm::normalize(v.Position), v.Normal) == doctest::Approx(1.0f).epsilon(1e-4));
	}
}

TEST_CASE("Sphere resolution is clamped to a valid minimum")
{
	MeshData tiny = MeshGenerator::CreateSphere(0, 0);
	CHECK(tiny.IsValid());
	CHECK(tiny.GetTriangleCount() > 0);
	CheckWinding(tiny, 0.0f); // very coarse facets only need to face outward

}

TEST_CASE("Plane mesh faces +Y")
{
	MeshData plane = MeshGenerator::CreatePlane();
	CHECK(plane.IsValid());
	CHECK(plane.GetTriangleCount() == 2);
	CheckNormalsAreUnit(plane);
	CheckWinding(plane);
	for (const Vertex& v : plane.Vertices)
	{
		CHECK(v.Position.y == 0.0f);
		CHECK(v.Normal.y == 1.0f);
	}
}

TEST_CASE("MeshData::IsValid rejects broken meshes")
{
	MeshData mesh = MeshGenerator::CreatePlane();
	CHECK(mesh.IsValid());

	MeshData badIndex = mesh;
	badIndex.Indices.push_back(99);
	badIndex.Indices.push_back(0);
	badIndex.Indices.push_back(1);
	CHECK_FALSE(badIndex.IsValid());

	MeshData partial = mesh;
	partial.Indices.pop_back();
	CHECK_FALSE(partial.IsValid());

	MeshData nan = mesh;
	nan.Vertices[0].Position.x = std::nanf("");
	CHECK_FALSE(nan.IsValid());
}

TEST_CASE("ComputeNormals: smooth, area-weighted and safe for degenerate triangles")
{
	MeshData mesh;
	mesh.Vertices = { { { 0, 0, 0 }, {}, {} }, { { 1, 0, 0 }, {}, {} }, { { 0, 1, 0 }, {}, {} }, { { 5, 5, 5 }, {}, {} } };
	mesh.Indices = { 0, 1, 2 };
	mesh.ComputeNormals();
	for (int i = 0; i < 3; i++)
		CHECK(mesh.Vertices[i].Normal == glm::vec3(0, 0, 1));
	CHECK(mesh.Vertices[3].Normal == glm::vec3(0, 1, 0)); // unreferenced vertex gets a safe default

	MeshData degenerate;
	degenerate.Vertices = { { { 0, 0, 0 }, {}, {} }, { { 1, 0, 0 }, {}, {} }, { { 2, 0, 0 }, {}, {} } }; // collinear
	degenerate.Indices = { 0, 1, 2, 0, 1, 99 }; // second triangle has an out-of-range index and is ignored
	degenerate.ComputeNormals();
	CHECK(degenerate.IsValid() == false); // the stray index is still invalid
	CHECK(glm::length(degenerate.Vertices[1].Normal) == doctest::Approx(1.0f));
}

TEST_CASE("ComputeTangents: tangent follows increasing U, handedness follows the UV winding")
{
	MeshData plane = MeshGenerator::CreatePlane();
	for (const Vertex& v : plane.Vertices)
	{
		CHECK(v.Tangent.x == doctest::Approx(1.0f)); // U increases along +X
		CHECK(v.Tangent.w == -1.0f);                 // V increases along +Z, so cross(N, T) opposes the bitangent
	}

	// Mirroring U flips both the tangent and keeps the frame orthonormal.
	MeshData mirrored = plane;
	for (Vertex& v : mirrored.Vertices)
		v.UV.x = 1.0f - v.UV.x;
	mirrored.ComputeTangents();
	CHECK(mirrored.Vertices[0].Tangent.x == doctest::Approx(-1.0f));
	CHECK(mirrored.Vertices[0].Tangent.w == 1.0f);
}

TEST_CASE("ComputeTangents: degenerate UVs still give a unit tangent perpendicular to the normal")
{
	MeshData mesh = MeshGenerator::CreateSphere(8, 6);
	for (Vertex& v : mesh.Vertices)
		v.UV = { 0.5f, 0.5f }; // collapsed mapping
	mesh.ComputeTangents();
	for (const Vertex& v : mesh.Vertices)
	{
		CHECK(glm::length(glm::vec3(v.Tangent)) == doctest::Approx(1.0f).epsilon(1e-4));
		CHECK(glm::dot(glm::vec3(v.Tangent), v.Normal) == doctest::Approx(0.0f).epsilon(1e-3));
	}
}

TEST_CASE("Generated primitives have orthonormal tangent frames")
{
	for (const MeshData& mesh : { MeshGenerator::CreateCube(), MeshGenerator::CreateSphere(16, 8), MeshGenerator::CreatePlane() })
	{
		for (const Vertex& v : mesh.Vertices)
		{
			CHECK(glm::length(glm::vec3(v.Tangent)) == doctest::Approx(1.0f).epsilon(1e-3));
			CHECK(std::abs(glm::dot(glm::vec3(v.Tangent), v.Normal)) < 1e-3f);
			CHECK(std::abs(v.Tangent.w) == 1.0f);
		}
	}
}
