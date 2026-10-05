#include "Starfall/Physics/PhysicsWorld.h"

#include "Starfall/Math/MathUtils.h"
#include "Starfall/Scene/Entity.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cstdarg>
#include <thread>
#include <unordered_map>

namespace Starfall {

	namespace {

		constexpr float FixedTimestep = 1.0f / 60.0f;
		constexpr int MaxStepsPerFrame = 4;

		namespace Layers {
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		namespace BroadPhaseLayers {
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr uint32_t Count = 2;
		}

		class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				if(a == Layers::NonMoving)
					return b == Layers::Moving;
				return true;
			}
		};

		class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
		{
		public:
			uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::NonMoving ? "NON_MOVING" : "MOVING";
			}
#endif
		};

		class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const override
			{
				if(layer == Layers::NonMoving)
					return bpLayer == BroadPhaseLayers::Moving;
				return true;
			}
		};

		class SkipSensorsFilter final : public JPH::BodyFilter
		{
		public:
			bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
		};

		// Jolt global state (factory + type registration) is reference counted across PhysicsWorld instances.
		std::mutex s_JoltMutex;
		int s_JoltRefCount = 0;

		void JoltTrace(const char* format, ...)
		{
			va_list args;
			va_start(args, format);
			char buffer[1024];
			vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			SF_CORE_TRACE("Jolt: {0}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			SF_CORE_ERROR("Jolt assert: {0} ({1}) at {2}:{3}", expression, message ? message : "", file, line);
			return true;
		}
#endif

		void JoltInit()
		{
			std::scoped_lock lock(s_JoltMutex);
			if(s_JoltRefCount++ == 0)
			{
				JPH::RegisterDefaultAllocator();
				JPH::Trace = JoltTrace;
				JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			}
		}

		void JoltShutdown()
		{
			std::scoped_lock lock(s_JoltMutex);
			if(--s_JoltRefCount == 0)
			{
				JPH::UnregisterTypes();
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

		JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::vec3 ToGlm(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
		glm::quat ToGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		struct WorldPose
		{
			glm::vec3 Position;
			glm::quat Rotation;
			glm::vec3 Scale;
		};

		WorldPose DecomposeWorld(const glm::mat4& world)
		{
			glm::vec3 skew;
			glm::vec4 perspective;
			WorldPose pose;
			if(!glm::decompose(world, pose.Scale, pose.Rotation, pose.Position, skew, perspective))
				pose = { glm::vec3(world[3]), glm::quat(1, 0, 0, 0), glm::vec3(1.0f) };
			return pose;
		}

	}

	class ContactListenerImpl final : public JPH::ContactListener
	{
	public:
		void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold&, JPH::ContactSettings&) override
		{
			Push(body1, body2, true);
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
		{
			std::scoped_lock lock(m_Mutex);
			auto a = m_BodyInfo.find(pair.GetBody1ID().GetIndexAndSequenceNumber());
			auto b = m_BodyInfo.find(pair.GetBody2ID().GetIndexAndSequenceNumber());
			if(a == m_BodyInfo.end() || b == m_BodyInfo.end())
				return;
			bool trigger = a->second.IsTrigger || b->second.IsTrigger;
			m_Events.push_back({ trigger ? CollisionEventType::TriggerEnd : CollisionEventType::CollisionEnd, a->second.Entity, b->second.Entity });
		}

		void Register(JPH::BodyID id, UUID entity, bool isTrigger)
		{
			std::scoped_lock lock(m_Mutex);
			m_BodyInfo[id.GetIndexAndSequenceNumber()] = { entity, isTrigger };
		}

		void Unregister(JPH::BodyID id)
		{
			std::scoped_lock lock(m_Mutex);
			m_BodyInfo.erase(id.GetIndexAndSequenceNumber());
		}

		std::vector<CollisionEvent> Take()
		{
			std::scoped_lock lock(m_Mutex);
			std::vector<CollisionEvent> events = std::move(m_Events);
			m_Events.clear();
			return events;
		}

		UUID Find(JPH::BodyID id) const
		{
			std::scoped_lock lock(m_Mutex);
			auto it = m_BodyInfo.find(id.GetIndexAndSequenceNumber());
			return it == m_BodyInfo.end() ? UUID(0) : it->second.Entity;
		}

	private:
		struct Info
		{
			UUID Entity = UUID(0);
			bool IsTrigger = false;
		};

		void Push(const JPH::Body& body1, const JPH::Body& body2, bool begin)
		{
			std::scoped_lock lock(m_Mutex);
			auto a = m_BodyInfo.find(body1.GetID().GetIndexAndSequenceNumber());
			auto b = m_BodyInfo.find(body2.GetID().GetIndexAndSequenceNumber());
			if(a == m_BodyInfo.end() || b == m_BodyInfo.end())
				return;
			bool trigger = a->second.IsTrigger || b->second.IsTrigger;
			CollisionEventType type = trigger ? (begin ? CollisionEventType::TriggerBegin : CollisionEventType::TriggerEnd)
											  : (begin ? CollisionEventType::CollisionBegin : CollisionEventType::CollisionEnd);
			m_Events.push_back({ type, a->second.Entity, b->second.Entity });
		}

		mutable std::mutex m_Mutex;
		std::unordered_map<uint32_t, Info> m_BodyInfo;
		std::vector<CollisionEvent> m_Events;
	};

	struct PhysicsWorld::Impl
	{
		ObjectLayerPairFilterImpl ObjectLayerPairFilter;
		BroadPhaseLayerInterfaceImpl BroadPhaseLayerInterface;
		ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadPhaseLayerFilter;
		ContactListenerImpl ContactListener;

		Scope<JPH::TempAllocatorImpl> TempAllocator;
		Scope<JPH::JobSystemThreadPool> JobSystem;
		Scope<JPH::PhysicsSystem> System;

		std::unordered_map<UUID, JPH::BodyID> Bodies;
	};

	PhysicsWorld::PhysicsWorld(Scene* scene)
		: m_Scene(scene)
	{
		JoltInit();
		m_Impl = CreateScope<Impl>();
	}

	PhysicsWorld::~PhysicsWorld()
	{
		if(m_Impl->System)
			OnStop();
		m_Impl.reset();
		JoltShutdown();
	}

	void PhysicsWorld::OnStart()
	{
		constexpr uint32_t MaxBodies = 32768;
		constexpr uint32_t MaxBodyPairs = 32768;
		constexpr uint32_t MaxContactConstraints = 16384;

		m_Impl->TempAllocator = CreateScope<JPH::TempAllocatorImpl>(64 * 1024 * 1024);
		uint32_t threads = std::max(1u, std::thread::hardware_concurrency()) - 1;
		m_Impl->JobSystem = CreateScope<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(std::min(threads, 4u)));
		m_Impl->System = CreateScope<JPH::PhysicsSystem>();
		m_Impl->System->Init(MaxBodies, 0, MaxBodyPairs, MaxContactConstraints, m_Impl->BroadPhaseLayerInterface, m_Impl->ObjectVsBroadPhaseLayerFilter, m_Impl->ObjectLayerPairFilter);
		m_Impl->System->SetContactListener(&m_Impl->ContactListener);
		m_Impl->System->SetGravity(ToJolt(m_Scene->GetSettings().Gravity));

		m_Scene->Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent&) { CreateBody(entity); });
		m_Scene->Each<ColliderComponent>([&](Entity entity, ColliderComponent&) {
			if(!entity.HasComponent<RigidBodyComponent>())
				CreateBody(entity);
		});
		m_Impl->System->OptimizeBroadPhase();

		m_DestroyCallback = m_Scene->AddDestroyCallback([this](Entity entity) { DestroyBody(entity); });
	}

	void PhysicsWorld::OnStop()
	{
		if(!m_Impl->System)
			return;
		m_Scene->RemoveDestroyCallback(m_DestroyCallback);
		m_DestroyCallback = 0;
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		for(auto& [uuid, id] : m_Impl->Bodies)
		{
			bodies.RemoveBody(id);
			bodies.DestroyBody(id);
		}
		m_Impl->Bodies.clear();
		m_Impl->System.reset();
		m_Impl->JobSystem.reset();
		m_Impl->TempAllocator.reset();
	}

	void PhysicsWorld::CreateBody(Entity entity)
	{
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		const RigidBodyComponent* rb = entity.TryGetComponent<RigidBodyComponent>();
		const ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>();
		if(!collider)
		{
			SF_CORE_WARN("Entity '{0}' has a RigidBody but no Collider; no physics body created", entity.GetName());
			return;
		}

		WorldPose pose = DecomposeWorld(m_Scene->GetWorldTransform(entity));
		glm::vec3 absScale = glm::abs(pose.Scale);
		auto positive = [](float v) { return std::max(v, 0.01f); };

		JPH::ShapeRefC shape;
		switch(collider->Shape)
		{
			case ColliderShape::Box:
			{
				glm::vec3 half = glm::max(collider->Size * absScale * 0.5f, glm::vec3(0.005f));
				shape = new JPH::BoxShape(ToJolt(half), std::min(0.05f, std::min({ half.x, half.y, half.z }) * 0.5f));
				break;
			}
			case ColliderShape::Sphere:
			{
				float radius = positive(collider->Size.x * 0.5f * std::max({ absScale.x, absScale.y, absScale.z }));
				shape = new JPH::SphereShape(radius);
				break;
			}
			case ColliderShape::Capsule:
			{
				float radius = positive(collider->Size.x * 0.5f * std::max(absScale.x, absScale.z));
				float totalHeight = collider->Size.y * absScale.y;
				float halfCylinder = std::max(0.005f, (totalHeight - 2.0f * radius) * 0.5f);
				shape = new JPH::CapsuleShape(halfCylinder, radius);
				break;
			}
		}
		if(glm::length(collider->Offset) > 0.0f)
			shape = new JPH::RotatedTranslatedShape(ToJolt(collider->Offset * pose.Scale), JPH::Quat::sIdentity(), shape);

		BodyType type = rb ? rb->Type : BodyType::Static;
		JPH::EMotionType motion = type == BodyType::Static ? JPH::EMotionType::Static : type == BodyType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
		// Triggers must be kinematic so they are always processed by the sensor logic.
		if(collider->IsTrigger && motion == JPH::EMotionType::Static)
			motion = JPH::EMotionType::Kinematic;
		JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? Layers::NonMoving : Layers::Moving;

		JPH::BodyCreationSettings settings(shape, ToJolt(pose.Position), ToJolt(pose.Rotation), motion, layer);
		settings.mFriction = collider->Friction;
		settings.mRestitution = collider->Restitution;
		settings.mIsSensor = collider->IsTrigger;
		settings.mUserData = static_cast<uint64_t>(entity.GetUUID());
		if(rb)
		{
			settings.mLinearDamping = rb->LinearDamping;
			settings.mAngularDamping = rb->AngularDamping;
			settings.mGravityFactor = rb->GravityFactor;
			if(motion == JPH::EMotionType::Dynamic)
			{
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = std::max(rb->Mass, 0.001f);
				JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::All;
				if(rb->LockRotationX) dofs &= ~JPH::EAllowedDOFs::RotationX;
				if(rb->LockRotationY) dofs &= ~JPH::EAllowedDOFs::RotationY;
				if(rb->LockRotationZ) dofs &= ~JPH::EAllowedDOFs::RotationZ;
				settings.mAllowedDOFs = dofs;
			}
			if(rb->Continuous)
				settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
		}

		JPH::Body* body = bodies.CreateBody(settings);
		if(!body)
		{
			SF_CORE_ERROR("Failed to create physics body for '{0}'", entity.GetName());
			return;
		}
		bodies.AddBody(body->GetID(), motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		m_Impl->Bodies[entity.GetUUID()] = body->GetID();
		m_Impl->ContactListener.Register(body->GetID(), entity.GetUUID(), collider->IsTrigger);
		if(auto* rbc = entity.TryGetComponent<RigidBodyComponent>())
			rbc->RuntimeBodyID = body->GetID().GetIndexAndSequenceNumber();
	}

	void PhysicsWorld::DestroyBody(Entity entity)
	{
		auto it = m_Impl->Bodies.find(entity.GetUUID());
		if(it == m_Impl->Bodies.end() || !m_Impl->System)
			return;
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		m_Impl->ContactListener.Unregister(it->second);
		bodies.RemoveBody(it->second);
		bodies.DestroyBody(it->second);
		m_Impl->Bodies.erase(it);
	}

	void PhysicsWorld::SyncKinematicBodies(float deltaTime)
	{
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		for(auto& [uuid, id] : m_Impl->Bodies)
		{
			if(bodies.GetMotionType(id) != JPH::EMotionType::Kinematic)
				continue;
			Entity entity = m_Scene->FindEntityByUUID(uuid);
			if(!entity)
				continue;
			WorldPose pose = DecomposeWorld(m_Scene->GetWorldTransform(entity));
			bodies.MoveKinematic(id, ToJolt(pose.Position), ToJolt(pose.Rotation), deltaTime);
		}
	}

	void PhysicsWorld::SyncTransformsFromBodies()
	{
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		for(auto& [uuid, id] : m_Impl->Bodies)
		{
			if(bodies.GetMotionType(id) != JPH::EMotionType::Dynamic || !bodies.IsActive(id))
				continue;
			Entity entity = m_Scene->FindEntityByUUID(uuid);
			if(!entity)
				continue;
			JPH::RVec3 position;
			JPH::Quat rotation;
			bodies.GetPositionAndRotation(id, position, rotation);
			glm::vec3 scale = DecomposeWorld(m_Scene->GetWorldTransform(entity)).Scale;
			glm::mat4 world = glm::translate(glm::mat4(1.0f), ToGlm(position)) * glm::mat4_cast(ToGlm(rotation)) * glm::scale(glm::mat4(1.0f), scale);
			m_Scene->SetWorldTransform(entity, world);
		}
	}

	void PhysicsWorld::OnUpdate(float deltaTime)
	{
		if(!m_Impl->System)
			return;

		// Entities spawned at runtime (scripts, prefabs) get their bodies lazily.
		m_Scene->Each<ColliderComponent>([&](Entity entity, ColliderComponent&) {
			if(!m_Impl->Bodies.contains(entity.GetUUID()))
				CreateBody(entity);
		});

		m_FrameEvents.clear();
		m_Accumulator += std::min(deltaTime, FixedTimestep * MaxStepsPerFrame);
		int steps = 0;
		while(m_Accumulator >= FixedTimestep && steps < MaxStepsPerFrame)
		{
			SyncKinematicBodies(FixedTimestep);
			m_Impl->System->Update(FixedTimestep, 1, m_Impl->TempAllocator.get(), m_Impl->JobSystem.get());
			m_Accumulator -= FixedTimestep;
			steps++;
		}
		CollectEvents();
		if(steps > 0)
			SyncTransformsFromBodies();
	}

	bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit) const
	{
		if(!m_Impl->System || maxDistance <= 0.0f || glm::length(direction) < 1e-6f)
			return false;
		glm::vec3 dir = glm::normalize(direction);
		JPH::RRayCast ray(ToJolt(origin), ToJolt(dir * maxDistance));
		JPH::RayCastResult result;
		SkipSensorsFilter bodyFilter;
		if(!m_Impl->System->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, bodyFilter))
			return false;

		JPH::Vec3 point = ray.GetPointOnRay(result.mFraction);
		outHit.Distance = result.mFraction * maxDistance;
		outHit.Point = ToGlm(point);
		outHit.HitEntity = m_Impl->ContactListener.Find(result.mBodyID);
		JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), result.mBodyID);
		outHit.Normal = lock.Succeeded() ? ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point)) : glm::vec3(0, 1, 0);
		return true;
	}

	std::vector<UUID> PhysicsWorld::OverlapSphere(const glm::vec3& center, float radius) const
	{
		std::vector<UUID> result;
		if(!m_Impl->System || radius <= 0.0f)
			return result;
		JPH::SphereShape sphere(radius);
		sphere.SetEmbedded();
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		JPH::CollideShapeSettings settings;
		m_Impl->System->GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(ToJolt(center)), settings, JPH::RVec3::sZero(), collector, {}, {}, SkipSensorsFilter());
		for(const auto& hit : collector.mHits)
		{
			UUID id = m_Impl->ContactListener.Find(hit.mBodyID2);
			if(!id.IsNull() && std::find(result.begin(), result.end(), id) == result.end())
				result.push_back(id);
		}
		return result;
	}

	bool PhysicsWorld::HasBody(Entity entity) const
	{
		return entity && m_Impl->Bodies.contains(entity.GetUUID());
	}

	void PhysicsWorld::AddForce(Entity entity, const glm::vec3& force)
	{
		if(!HasBody(entity)) return;
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		JPH::BodyID id = m_Impl->Bodies[entity.GetUUID()];
		if(bodies.GetMotionType(id) == JPH::EMotionType::Dynamic)
			bodies.AddForce(id, ToJolt(force));
	}

	void PhysicsWorld::AddImpulse(Entity entity, const glm::vec3& impulse)
	{
		if(!HasBody(entity)) return;
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		JPH::BodyID id = m_Impl->Bodies[entity.GetUUID()];
		if(bodies.GetMotionType(id) == JPH::EMotionType::Dynamic)
			bodies.AddImpulse(id, ToJolt(impulse));
	}

	void PhysicsWorld::AddTorque(Entity entity, const glm::vec3& torque)
	{
		if(!HasBody(entity)) return;
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		JPH::BodyID id = m_Impl->Bodies[entity.GetUUID()];
		if(bodies.GetMotionType(id) == JPH::EMotionType::Dynamic)
			bodies.AddTorque(id, ToJolt(torque));
	}

	void PhysicsWorld::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
	{
		if(!HasBody(entity)) return;
		JPH::BodyID id = m_Impl->Bodies[entity.GetUUID()];
		m_Impl->System->GetBodyInterface().SetLinearVelocity(id, ToJolt(velocity));
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(Entity entity) const
	{
		if(!HasBody(entity)) return glm::vec3(0.0f);
		return ToGlm(m_Impl->System->GetBodyInterface().GetLinearVelocity(m_Impl->Bodies.at(entity.GetUUID())));
	}

	void PhysicsWorld::SetAngularVelocity(Entity entity, const glm::vec3& velocity)
	{
		if(!HasBody(entity)) return;
		m_Impl->System->GetBodyInterface().SetAngularVelocity(m_Impl->Bodies[entity.GetUUID()], ToJolt(velocity));
	}

	glm::vec3 PhysicsWorld::GetAngularVelocity(Entity entity) const
	{
		if(!HasBody(entity)) return glm::vec3(0.0f);
		return ToGlm(m_Impl->System->GetBodyInterface().GetAngularVelocity(m_Impl->Bodies.at(entity.GetUUID())));
	}

	void PhysicsWorld::Teleport(Entity entity)
	{
		if(!HasBody(entity)) return;
		WorldPose pose = DecomposeWorld(m_Scene->GetWorldTransform(entity));
		JPH::BodyID id = m_Impl->Bodies[entity.GetUUID()];
		JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
		bodies.SetPositionAndRotation(id, ToJolt(pose.Position), ToJolt(pose.Rotation), JPH::EActivation::Activate);
		if(bodies.GetMotionType(id) == JPH::EMotionType::Dynamic)
		{
			bodies.SetLinearVelocity(id, JPH::Vec3::sZero());
			bodies.SetAngularVelocity(id, JPH::Vec3::sZero());
		}
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		if(m_Impl->System)
		{
			m_Impl->System->SetGravity(ToJolt(gravity));
			// Sleeping bodies would otherwise ignore the change.
			JPH::BodyInterface& bodies = m_Impl->System->GetBodyInterface();
			for(auto& [uuid, id] : m_Impl->Bodies)
				if(bodies.GetMotionType(id) == JPH::EMotionType::Dynamic)
					bodies.ActivateBody(id);
		}
	}

	uint32_t PhysicsWorld::GetBodyCount() const
	{
		return static_cast<uint32_t>(m_Impl->Bodies.size());
	}

	void PhysicsWorld::CollectEvents()
	{
		auto events = m_Impl->ContactListener.Take();
		m_FrameEvents.insert(m_FrameEvents.end(), events.begin(), events.end());
	}

}
