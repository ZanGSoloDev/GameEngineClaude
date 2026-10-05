#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Core/UUID.h"
#include "Starfall/Scene/Components.h"

#include <functional>
#include <glm/glm.hpp>
#include <mutex>
#include <vector>

namespace Starfall {

	class Scene;
	class Entity;

	struct RaycastHit
	{
		UUID HitEntity = UUID(0);
		glm::vec3 Point = { 0, 0, 0 };
		glm::vec3 Normal = { 0, 1, 0 };
		float Distance = 0.0f;
	};

	enum class CollisionEventType { CollisionBegin, CollisionEnd, TriggerBegin, TriggerEnd };

	struct CollisionEvent
	{
		CollisionEventType Type;
		UUID EntityA = UUID(0);
		UUID EntityB = UUID(0);
	};

	// Wraps Jolt Physics. Created by Scene::OnRuntimeStart: bodies are created from RigidBody/Collider components.
	class PhysicsWorld
	{
	public:
		explicit PhysicsWorld(Scene* scene);
		~PhysicsWorld();
		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		void OnStart();
		void OnStop();
		void OnUpdate(float deltaTime);

		// Scene queries
		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit) const;
		std::vector<UUID> OverlapSphere(const glm::vec3& center, float radius) const;

		// Body control (no-ops when the entity has no body).
		bool HasBody(Entity entity) const;
		void AddForce(Entity entity, const glm::vec3& force);
		void AddImpulse(Entity entity, const glm::vec3& impulse);
		void AddTorque(Entity entity, const glm::vec3& torque);
		void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity(Entity entity) const;
		void SetAngularVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetAngularVelocity(Entity entity) const;
		// Moves the body to the entity's current world transform (call after changing Transform from code).
		void Teleport(Entity entity);
		void SetGravity(const glm::vec3& gravity);

		uint32_t GetBodyCount() const;

		// Collision/trigger events produced by the most recent OnUpdate (replaced on every update).
		const std::vector<CollisionEvent>& GetEvents() const { return m_FrameEvents; }

	private:
		void CreateBody(Entity entity);
		void DestroyBody(Entity entity);
		void SyncKinematicBodies(float deltaTime);
		void SyncTransformsFromBodies();
		void CollectEvents();

		struct Impl;
		Scope<Impl> m_Impl;
		Scene* m_Scene;
		float m_Accumulator = 0.0f;
		std::vector<CollisionEvent> m_FrameEvents;
		uint32_t m_DestroyCallback = 0;
	};

}
