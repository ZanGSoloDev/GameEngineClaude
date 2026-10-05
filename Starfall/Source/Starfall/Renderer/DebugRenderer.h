#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Math/MathUtils.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <vector>

namespace Starfall {

	class RenderContext;

	struct DebugVertex
	{
		glm::vec3 Position;
		uint32_t Color; // RGBA8, little endian (r in the low byte)
	};
	static_assert(sizeof(DebugVertex) == 16);

	// Immediate-mode line renderer: shapes queued during a frame are drawn (and cleared) by SceneRenderer.
	class DebugRenderer
	{
	public:
		static uint32_t PackColor(const glm::vec4& color);

		void DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, bool depthTested = true);
		void DrawAABB(const Math::AABB& box, const glm::vec4& color, bool depthTested = true);
		void DrawBox(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color, bool depthTested = true);
		void DrawCircle(const glm::vec3& center, const glm::vec3& axisU, const glm::vec3& axisV, float radius, const glm::vec4& color, bool depthTested = true, uint32_t segments = 48);
		void DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color, bool depthTested = true);
		void DrawCapsule(const glm::mat4& transform, float radius, float cylinderHeight, const glm::vec4& color, bool depthTested = true);
		void DrawCone(const glm::vec3& apex, const glm::vec3& direction, float length, float angleRadians, const glm::vec4& color, bool depthTested = true);
		void DrawFrustum(const glm::mat4& viewProjection, const glm::vec4& color, bool depthTested = true);
		void DrawArrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool depthTested = true);

		void Clear();
		size_t GetLineCount() const { return (m_DepthLines.size() + m_OverlayLines.size()) / 2; }

		const std::vector<DebugVertex>& GetDepthTestedVertices() const { return m_DepthLines; }
		const std::vector<DebugVertex>& GetOverlayVertices() const { return m_OverlayLines; }

	private:
		std::vector<DebugVertex>& Target(bool depthTested) { return depthTested ? m_DepthLines : m_OverlayLines; }

		std::vector<DebugVertex> m_DepthLines;
		std::vector<DebugVertex> m_OverlayLines;
	};

}
