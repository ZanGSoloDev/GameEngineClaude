#include "EditorCamera.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace StarfallEditor {

	EditorCamera::EditorCamera()
	{
		// Look at the origin from the default position.
		glm::vec3 dir = glm::normalize(glm::vec3(0, 1.5f, 0) - m_Position);
		m_Yaw = std::atan2(-dir.x, -dir.z);
		m_Pitch = std::asin(dir.y);
	}

	glm::vec3 EditorCamera::GetForward() const
	{
		return glm::normalize(glm::vec3(-std::sin(m_Yaw) * std::cos(m_Pitch), std::sin(m_Pitch), -std::cos(m_Yaw) * std::cos(m_Pitch)));
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
	}

	void EditorCamera::SetPose(const glm::vec3& position, float yaw, float pitch)
	{
		m_Position = position;
		m_Yaw = yaw;
		m_Pitch = std::clamp(pitch, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
	}

	void EditorCamera::Update(float dt, const Input& in)
	{
		const float lookSensitivity = 0.0035f;
		glm::vec3 forward = GetForward();
		glm::vec3 right = GetRight();
		glm::vec3 up = glm::cross(right, forward);

		if(in.RightButton)
		{
			m_Yaw -= in.MouseDelta.x * lookSensitivity;
			m_Pitch = std::clamp(m_Pitch - in.MouseDelta.y * lookSensitivity, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
			if(in.Scroll != 0.0f)
				m_MoveSpeed = std::clamp(m_MoveSpeed * std::pow(1.2f, in.Scroll), 0.5f, 200.0f);
			float speed = m_MoveSpeed * (in.Shift ? 3.0f : 1.0f);
			glm::vec3 move = right * in.Move.x + glm::vec3(0, 1, 0) * in.Move.y + forward * in.Move.z;
			if(glm::dot(move, move) > 0.0f)
				m_Position += glm::normalize(move) * speed * dt;
		}
		else if(in.MiddleButton)
		{
			float panScale = 0.0025f * std::max(m_MoveSpeed * 0.25f, 1.0f);
			m_Position += (-right * in.MouseDelta.x + up * in.MouseDelta.y) * panScale;
		}
		else if(in.LeftButton && in.Alt)
		{
			// Orbit around the point 8 units in front of the camera.
			glm::vec3 pivot = m_Position + forward * 8.0f;
			m_Yaw -= in.MouseDelta.x * lookSensitivity;
			m_Pitch = std::clamp(m_Pitch + in.MouseDelta.y * lookSensitivity, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
			m_Position = pivot - GetForward() * 8.0f;
		}
		else if(in.Scroll != 0.0f)
		{
			m_Position += forward * in.Scroll * std::max(m_MoveSpeed * 0.4f, 0.5f);
		}
	}

	void EditorCamera::Focus(const glm::vec3& center, float radius)
	{
		radius = std::max(radius, 0.25f);
		float distance = radius / std::sin(glm::radians(m_FovDegrees) * 0.5f) * 1.1f;
		m_Position = center - GetForward() * distance;
	}

	Starfall::RenderCamera EditorCamera::GetRenderCamera(float aspectRatio) const
	{
		Starfall::RenderCamera camera;
		camera.Position = m_Position;
		camera.View = glm::lookAt(m_Position, m_Position + GetForward(), glm::vec3(0, 1, 0));
		camera.Projection = glm::perspective(glm::radians(m_FovDegrees), std::max(aspectRatio, 0.01f), m_Near, m_Far);
		camera.Near = m_Near;
		camera.Far = m_Far;
		return camera;
	}

	Starfall::Math::Ray EditorCamera::ScreenRay(const glm::vec2& uv, float aspectRatio) const
	{
		Starfall::RenderCamera camera = GetRenderCamera(aspectRatio);
		glm::mat4 inverse = glm::inverse(camera.Projection * camera.View);
		glm::vec4 ndcNear(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
		glm::vec4 ndcFar(ndcNear.x, ndcNear.y, 1.0f, 1.0f);
		glm::vec4 a = inverse * ndcNear;
		glm::vec4 b = inverse * ndcFar;
		glm::vec3 origin = glm::vec3(a) / a.w;
		glm::vec3 target = glm::vec3(b) / b.w;
		return { origin, glm::normalize(target - origin) };
	}

}
