#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Starfall::Math {

	constexpr float Pi = 3.14159265358979323846f;

	// Splits a matrix into translation / Euler rotation (radians) / scale. Returns false for singular matrices.
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);

	struct Ray
	{
		glm::vec3 Origin = { 0, 0, 0 };
		glm::vec3 Direction = { 0, 0, -1 };
	};

	struct AABB
	{
		glm::vec3 Min = { 1e30f, 1e30f, 1e30f };
		glm::vec3 Max = { -1e30f, -1e30f, -1e30f };

		bool IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
		void Expand(const glm::vec3& p) { Min = glm::min(Min, p); Max = glm::max(Max, p); }
		void Expand(const AABB& other) { if(other.IsValid()) { Expand(other.Min); Expand(other.Max); } }
		glm::vec3 Center() const { return (Min + Max) * 0.5f; }
		glm::vec3 Extents() const { return (Max - Min) * 0.5f; }
		AABB Transformed(const glm::mat4& m) const;
	};

	// Ray vs AABB slab test. Returns true and the entry distance on hit.
	bool IntersectRayAABB(const Ray& ray, const AABB& box, float& outT);
	// Moller-Trumbore. Returns true and the distance on hit (front and back faces).
	bool IntersectRayTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& outT);

	struct Frustum
	{
		glm::vec4 Planes[6]; // left, right, bottom, top, near, far; (normal.xyz, d), normals point inward

		static Frustum FromViewProjection(const glm::mat4& viewProjection);
		bool Intersects(const AABB& box) const;
	};

}
