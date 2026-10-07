#include <doctest/doctest.h>

#include "Lumen/Renderer/Shadow.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <initializer_list>

using namespace Lumen;

namespace {

	glm::mat4 CameraAt(const glm::vec3& position, const glm::vec3& euler = glm::vec3(0.0f))
	{
		return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(glm::quat(euler));
	}

	glm::vec3 Project(const ShadowCascade& cascade, const glm::vec3& worldPoint)
	{
		const glm::vec4 clip = cascade.ViewProj * glm::vec4(worldPoint, 1.0f);
		return glm::vec3(clip) / clip.w;
	}

	const float s_Fov = glm::radians(60.0f);

}

TEST_CASE("Bounds: empty, expand, corners and transform")
{
	Bounds b;
	CHECK(b.IsEmpty());
	b.Expand(glm::vec3(1, 2, 3));
	CHECK_FALSE(b.IsEmpty());
	b.Expand(glm::vec3(-1, 0, 5));
	CHECK(b.Min == glm::vec3(-1, 0, 3));
	CHECK(b.Max == glm::vec3(1, 2, 5));
	CHECK(b.Corner(0) == b.Min);
	CHECK(b.Corner(7) == b.Max);
	CHECK(b.Corner(1) == glm::vec3(1, 0, 3));

	Bounds unit;
	unit.Expand(glm::vec3(-0.5f));
	unit.Expand(glm::vec3(0.5f));
	const Bounds moved = unit.Transformed(glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
	CHECK(moved.Min == glm::vec3(9, -1, -1));
	CHECK(moved.Max == glm::vec3(11, 1, 1));
	CHECK(Bounds{}.Transformed(glm::mat4(1.0f)).IsEmpty());

	Bounds merged;
	merged.Expand(Bounds{});          // merging an empty box changes nothing
	CHECK(merged.IsEmpty());
	merged.Expand(unit);
	CHECK(merged.Max == glm::vec3(0.5f));
}

TEST_CASE("Shadow fit covers the whole frustum slice and rotating the light keeps it covered")
{
	for (const glm::vec3& light : { glm::vec3(0, -1, 0), glm::normalize(glm::vec3(1, -1, 0.5f)), glm::normalize(glm::vec3(-0.3f, -0.2f, -1)) })
	{
		ShadowCascade cascade;
		const glm::mat4 camera = CameraAt({ 3, 2, 5 }, { -0.2f, 0.7f, 0 });
		REQUIRE(ComputeDirectionalShadow(camera, s_Fov, 16.0f / 9.0f, 0.1f, 1000.0f, 40.0f, light, Bounds{}, 1024, cascade));

		// Every corner of the covered frustum slice projects inside the shadow map.
		const float h = std::tan(s_Fov / 2) * 40.0f, w = h * 16.0f / 9.0f;
		for (float sx : { -1.0f, 1.0f })
			for (float sy : { -1.0f, 1.0f })
			{
				const glm::vec3 p = Project(cascade, glm::vec3(camera * glm::vec4(sx * w, sy * h, -40.0f, 1.0f)));
				CHECK(std::abs(p.x) <= 1.0f);
				CHECK(std::abs(p.y) <= 1.0f);
				CHECK(p.z >= 0.0f);
				CHECK(p.z <= 1.0f);
			}
		CHECK(cascade.TexelWorldSize == doctest::Approx(2.0f * cascade.Radius / 1024.0f));
	}
}

TEST_CASE("Shadow fit: size depends only on the frustum, not on camera rotation")
{
	ShadowCascade a, b;
	REQUIRE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.5f, 0.1f, 500.0f, 30.0f, { 0.2f, -1, 0.1f }, Bounds{}, 2048, a));
	REQUIRE(ComputeDirectionalShadow(CameraAt({ 5, 1, -3 }, { 0.4f, 2.0f, 0.0f }), s_Fov, 1.5f, 0.1f, 500.0f, 30.0f, { 0.2f, -1, 0.1f }, Bounds{}, 2048, b));
	CHECK(a.Radius == b.Radius);
	CHECK(a.DepthRange == b.DepthRange);
}

TEST_CASE("Shadow fit: translation by less than a texel does not move the map (no shimmering)")
{
	const glm::vec3 light = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));
	ShadowCascade base;
	REQUIRE(ComputeDirectionalShadow(CameraAt({ 0, 2, 0 }), s_Fov, 1.5f, 0.1f, 500.0f, 30.0f, light, Bounds{}, 1024, base));

	// A world point's shadow-map coordinates should only change in whole-texel steps as the camera slides.
	const glm::vec3 probe(1.0f, 0.0f, -3.0f);
	const glm::vec3 reference = Project(base, probe);
	const float texelNdc = 2.0f / 1024.0f;
	int shifts = 0;
	for (int i = 1; i <= 40; i++)
	{
		ShadowCascade moved;
		const float offset = base.TexelWorldSize * 0.05f * static_cast<float>(i); // creeping by 5% of a texel per step
		REQUIRE(ComputeDirectionalShadow(CameraAt({ offset, 2, 0 }), s_Fov, 1.5f, 0.1f, 500.0f, 30.0f, light, Bounds{}, 1024, moved));
		const glm::vec3 now = Project(moved, probe);
		const float steps = (now.x - reference.x) / texelNdc;
		CHECK(std::abs(steps - std::round(steps)) < 0.01f); // whole texels only
		if (std::abs(steps) > 0.5f)
			shifts++;
	}
	CHECK(shifts < 40); // and not on every step
}

TEST_CASE("Shadow fit: casters toward the light extend the depth range, far-away casters are limited")
{
	ShadowCascade without, with, far;
	const glm::vec3 light(0, -1, 0);
	const glm::mat4 camera = CameraAt({ 0, 1, 0 });
	REQUIRE(ComputeDirectionalShadow(camera, s_Fov, 1.5f, 0.1f, 500.0f, 20.0f, light, Bounds{}, 1024, without));

	Bounds tower; // a tall object above the view region, between the light and the scene
	tower.Expand(glm::vec3(-1, 15, -6));
	tower.Expand(glm::vec3(1, 25, -4));
	REQUIRE(ComputeDirectionalShadow(camera, s_Fov, 1.5f, 0.1f, 500.0f, 20.0f, light, tower, 1024, with));
	CHECK(with.DepthRange > without.DepthRange);
	// The tower projects with a valid depth in [0,1] (not clipped by the near plane).
	const glm::vec3 p = Project(with, glm::vec3(0, 24, -5));
	CHECK(p.z >= 0.0f);
	CHECK(p.z <= 1.0f);

	Bounds huge;
	huge.Expand(glm::vec3(-1e5f, -1e5f, -1e5f));
	huge.Expand(glm::vec3(1e5f, 1e5f, 1e5f));
	REQUIRE(ComputeDirectionalShadow(camera, s_Fov, 1.5f, 0.1f, 500.0f, 20.0f, light, huge, 1024, far));
	CHECK(far.DepthRange <= 8.0f * far.Radius + 1e-3f); // capped at +-4 radii
}

TEST_CASE("Shadow fit: vertical light and degenerate input")
{
	ShadowCascade cascade;
	CHECK(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.0f, 0.1f, 100.0f, 50.0f, { 0, -1, 0 }, Bounds{}, 512, cascade));
	CHECK(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.0f, 0.1f, 100.0f, 50.0f, { 0, 1, 0 }, Bounds{}, 512, cascade));
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			CHECK(std::isfinite(cascade.ViewProj[c][r]));

	CHECK_FALSE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.0f, 0.1f, 100.0f, 50.0f, { 0, 0, 0 }, Bounds{}, 512, cascade));
	CHECK_FALSE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), 0.0f, 1.0f, 0.1f, 100.0f, 50.0f, { 0, -1, 0 }, Bounds{}, 512, cascade));
	CHECK_FALSE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 0.0f, 0.1f, 100.0f, 50.0f, { 0, -1, 0 }, Bounds{}, 512, cascade));
	CHECK_FALSE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.0f, 0.0f, 100.0f, 50.0f, { 0, -1, 0 }, Bounds{}, 512, cascade));
	CHECK_FALSE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.0f, 0.1f, 100.0f, 0.05f, { 0, -1, 0 }, Bounds{}, 512, cascade));
	CHECK_FALSE(ComputeDirectionalShadow(CameraAt({ 0, 0, 0 }), s_Fov, 1.0f, 0.1f, 100.0f, 50.0f, { 0, -1, 0 }, Bounds{}, 8, cascade));
}
