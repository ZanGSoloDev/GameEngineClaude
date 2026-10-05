#pragma once

#include "Starfall/Math/MathUtils.h"
#include "Starfall/Renderer/RenderTypes.h"

#include <glm/glm.hpp>

namespace StarfallEditor {

	// Unity/Godot-style viewport camera: RMB + WASD fly, MMB pan, Alt+LMB orbit, scroll to dolly.
	class EditorCamera
	{
	public:
		struct Input
		{
			glm::vec2 MouseDelta = { 0, 0 };
			float Scroll = 0.0f;
			bool RightButton = false;
			bool MiddleButton = false;
			bool LeftButton = false;
			bool Alt = false;
			bool Shift = false;
			glm::vec3 Move = { 0, 0, 0 };  // x right, y up, z forward (WASD/QE)
		};

		EditorCamera();

		void Update(float deltaTime, const Input& input);
		// Frames a bounding sphere (F key / double click).
		void Focus(const glm::vec3& center, float radius);

		Starfall::RenderCamera GetRenderCamera(float aspectRatio) const;
		glm::vec3 GetPosition() const { return m_Position; }
		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;
		float GetMoveSpeed() const { return m_MoveSpeed; }

		// Ray through a normalized viewport position (0..1, origin top-left).
		Starfall::Math::Ray ScreenRay(const glm::vec2& uv, float aspectRatio) const;

		void SetPose(const glm::vec3& position, float yaw, float pitch);
		float GetYaw() const { return m_Yaw; }
		float GetPitch() const { return m_Pitch; }

	private:
		glm::vec3 m_Position = { 6, 5, 10 };
		float m_Yaw = 0.0f;     // radians, 0 looks down -Z
		float m_Pitch = 0.0f;   // radians, positive looks up
		float m_MoveSpeed = 8.0f;
		float m_FovDegrees = 50.0f;
		float m_Near = 0.05f;
		float m_Far = 1000.0f;
	};

}
