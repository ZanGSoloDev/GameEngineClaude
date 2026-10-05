#include "Starfall/Math/MathUtils.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>

namespace Starfall::Math {

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::quat orientation;
		if(!glm::decompose(transform, scale, orientation, translation, skew, perspective))
			return false;
		rotation = glm::eulerAngles(orientation);
		return true;
	}

	AABB AABB::Transformed(const glm::mat4& m) const
	{
		AABB result;
		if(!IsValid())
			return result;
		for(int i = 0; i < 8; i++)
		{
			glm::vec3 corner((i & 1) ? Max.x : Min.x, (i & 2) ? Max.y : Min.y, (i & 4) ? Max.z : Min.z);
			result.Expand(glm::vec3(m * glm::vec4(corner, 1.0f)));
		}
		return result;
	}

	bool IntersectRayAABB(const Ray& ray, const AABB& box, float& outT)
	{
		float tMin = 0.0f;
		float tMax = 1e30f;
		for(int axis = 0; axis < 3; axis++)
		{
			float o = ray.Origin[axis];
			float d = ray.Direction[axis];
			if(std::abs(d) < 1e-8f)
			{
				if(o < box.Min[axis] || o > box.Max[axis])
					return false;
				continue;
			}
			float inv = 1.0f / d;
			float t0 = (box.Min[axis] - o) * inv;
			float t1 = (box.Max[axis] - o) * inv;
			if(t0 > t1)
				std::swap(t0, t1);
			tMin = std::max(tMin, t0);
			tMax = std::min(tMax, t1);
			if(tMin > tMax)
				return false;
		}
		outT = tMin;
		return true;
	}

	bool IntersectRayTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& outT)
	{
		const float epsilon = 1e-8f;
		glm::vec3 e1 = b - a;
		glm::vec3 e2 = c - a;
		glm::vec3 p = glm::cross(ray.Direction, e2);
		float det = glm::dot(e1, p);
		if(std::abs(det) < epsilon)
			return false;
		float invDet = 1.0f / det;
		glm::vec3 t = ray.Origin - a;
		float u = glm::dot(t, p) * invDet;
		if(u < 0.0f || u > 1.0f)
			return false;
		glm::vec3 q = glm::cross(t, e1);
		float v = glm::dot(ray.Direction, q) * invDet;
		if(v < 0.0f || u + v > 1.0f)
			return false;
		float distance = glm::dot(e2, q) * invDet;
		if(distance < 0.0f)
			return false;
		outT = distance;
		return true;
	}

	Frustum Frustum::FromViewProjection(const glm::mat4& m)
	{
		// Gribb/Hartmann extraction for a zero-to-one depth clip space (GLM_FORCE_DEPTH_ZERO_TO_ONE).
		Frustum f;
		glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
		glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
		glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
		glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
		f.Planes[0] = row3 + row0;
		f.Planes[1] = row3 - row0;
		f.Planes[2] = row3 + row1;
		f.Planes[3] = row3 - row1;
		f.Planes[4] = row2;
		f.Planes[5] = row3 - row2;
		for(auto& plane : f.Planes)
		{
			float length = glm::length(glm::vec3(plane));
			if(length > 0.0f)
				plane /= length;
		}
		return f;
	}

	bool Frustum::Intersects(const AABB& box) const
	{
		if(!box.IsValid())
			return false;
		for(const glm::vec4& plane : Planes)
		{
			glm::vec3 positive(plane.x >= 0 ? box.Max.x : box.Min.x, plane.y >= 0 ? box.Max.y : box.Min.y, plane.z >= 0 ? box.Max.z : box.Min.z);
			if(glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f)
				return false;
		}
		return true;
	}

}
