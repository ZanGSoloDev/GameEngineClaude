#include "Starfall/Renderer/DebugRenderer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Starfall {

	uint32_t DebugRenderer::PackColor(const glm::vec4& color)
	{
		auto channel = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
		return channel(color.r) | (channel(color.g) << 8) | (channel(color.b) << 16) | (channel(color.a) << 24);
	}

	void DebugRenderer::DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, bool depthTested)
	{
		uint32_t packed = PackColor(color);
		auto& target = Target(depthTested);
		target.push_back({ a, packed });
		target.push_back({ b, packed });
	}

	void DebugRenderer::DrawAABB(const Math::AABB& box, const glm::vec4& color, bool depthTested)
	{
		if(!box.IsValid())
			return;
		DrawBox(glm::translate(glm::mat4(1.0f), box.Center()), box.Extents(), color, depthTested);
	}

	void DebugRenderer::DrawBox(const glm::mat4& transform, const glm::vec3& h, const glm::vec4& color, bool depthTested)
	{
		glm::vec3 c[8];
		for(int i = 0; i < 8; i++)
			c[i] = glm::vec3(transform * glm::vec4((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z, 1.0f));
		static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for(const auto& e : edges)
			DrawLine(c[e[0]], c[e[1]], color, depthTested);
	}

	void DebugRenderer::DrawCircle(const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius, const glm::vec4& color, bool depthTested, uint32_t segments)
	{
		segments = std::max(segments, 3u);
		glm::vec3 previous = center + u * radius;
		for(uint32_t i = 1; i <= segments; i++)
		{
			float a = static_cast<float>(i) / segments * glm::two_pi<float>();
			glm::vec3 point = center + (u * std::cos(a) + v * std::sin(a)) * radius;
			DrawLine(previous, point, color, depthTested);
			previous = point;
		}
	}

	void DebugRenderer::DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color, bool depthTested)
	{
		DrawCircle(center, { 1, 0, 0 }, { 0, 1, 0 }, radius, color, depthTested);
		DrawCircle(center, { 1, 0, 0 }, { 0, 0, 1 }, radius, color, depthTested);
		DrawCircle(center, { 0, 1, 0 }, { 0, 0, 1 }, radius, color, depthTested);
	}

	void DebugRenderer::DrawCapsule(const glm::mat4& transform, float radius, float cylinderHeight, const glm::vec4& color, bool depthTested)
	{
		glm::vec3 x = glm::vec3(transform[0]);
		glm::vec3 y = glm::vec3(transform[1]);
		glm::vec3 z = glm::vec3(transform[2]);
		glm::vec3 p = glm::vec3(transform[3]);
		glm::vec3 top = p + y * (cylinderHeight * 0.5f);
		glm::vec3 bottom = p - y * (cylinderHeight * 0.5f);
		DrawCircle(top, x, z, radius, color, depthTested);
		DrawCircle(bottom, x, z, radius, color, depthTested);
		for(const glm::vec3& side : { x, -x, z, -z })
			DrawLine(top + side * radius, bottom + side * radius, color, depthTested);
		// hemispheres
		for(int hemisphere = 0; hemisphere < 2; hemisphere++)
		{
			glm::vec3 center = hemisphere == 0 ? top : bottom;
			float sign = hemisphere == 0 ? 1.0f : -1.0f;
			for(const glm::vec3& axis : { x, z })
			{
				glm::vec3 previous = center + axis * radius;
				for(uint32_t i = 1; i <= 16; i++)
				{
					float a = static_cast<float>(i) / 16.0f * glm::pi<float>();
					glm::vec3 point = center + axis * (std::cos(a) * radius) + y * (sign * std::sin(a) * radius);
					DrawLine(previous, point, color, depthTested);
					previous = point;
				}
			}
		}
	}

	void DebugRenderer::DrawCone(const glm::vec3& apex, const glm::vec3& direction, float length, float angle, const glm::vec4& color, bool depthTested)
	{
		glm::vec3 d = glm::normalize(direction);
		glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
		glm::vec3 u = glm::normalize(glm::cross(d, up));
		glm::vec3 v = glm::cross(d, u);
		float radius = std::tan(angle) * length;
		glm::vec3 center = apex + d * length;
		DrawCircle(center, u, v, radius, color, depthTested, 32);
		for(const glm::vec3& side : { u, -u, v, -v })
			DrawLine(apex, center + side * radius, color, depthTested);
	}

	void DebugRenderer::DrawFrustum(const glm::mat4& viewProjection, const glm::vec4& color, bool depthTested)
	{
		glm::mat4 inverse = glm::inverse(viewProjection);
		glm::vec3 c[8];
		for(int i = 0; i < 8; i++)
		{
			glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
			glm::vec4 world = inverse * ndc;
			c[i] = glm::vec3(world) / world.w;
		}
		static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for(const auto& e : edges)
			DrawLine(c[e[0]], c[e[1]], color, depthTested);
	}

	void DebugRenderer::DrawArrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool depthTested)
	{
		DrawLine(from, to, color, depthTested);
		glm::vec3 d = to - from;
		float length = glm::length(d);
		if(length < 1e-5f)
			return;
		d /= length;
		glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
		glm::vec3 u = glm::normalize(glm::cross(d, up));
		glm::vec3 v = glm::cross(d, u);
		float head = std::min(length * 0.25f, 0.3f);
		glm::vec3 base = to - d * head;
		for(const glm::vec3& side : { u, -u, v, -v })
			DrawLine(to, base + side * head * 0.4f, color, depthTested);
	}

	void DebugRenderer::Clear()
	{
		m_DepthLines.clear();
		m_OverlayLines.clear();
	}

}

