#include <doctest/doctest.h>

#include "Starfall/Physics/PhysicsWorld.h"
#include "Starfall/Scene/Entity.h"

using namespace Starfall;

namespace {

	Entity MakeFloor(Scene& scene)
	{
		Entity floor = scene.CreateEntity("Floor");
		floor.Transform().Translation = { 0, -0.5f, 0 };
		floor.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
		floor.AddComponent<ColliderComponent>().Size = { 40, 1, 40 };
		return floor;
	}

	Entity MakeBox(Scene& scene, const glm::vec3& position, const char* name = "Box")
	{
		Entity box = scene.CreateEntity(name);
		box.Transform().Translation = position;
		box.AddComponent<RigidBodyComponent>();
		box.AddComponent<ColliderComponent>().Size = { 1, 1, 1 };
		return box;
	}

	void Simulate(Scene& scene, float seconds)
	{
		int frames = static_cast<int>(seconds * 60.0f);
		for(int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(1.0f / 60.0f);
	}

}

TEST_CASE("Dynamic body falls and rests on a static floor")
{
	Scene scene;
	MakeFloor(scene);
	Entity box = MakeBox(scene, { 0, 5, 0 });
	scene.OnRuntimeStart();
	REQUIRE(scene.GetPhysicsWorld());
	CHECK(scene.GetPhysicsWorld()->GetBodyCount() == 2);

	Simulate(scene, 0.5f);
	CHECK(box.Transform().Translation.y < 5.0f); // already falling
	Simulate(scene, 4.0f);
	CHECK(box.Transform().Translation.y == doctest::Approx(0.5f).epsilon(0.05));
	CHECK(glm::length(scene.GetPhysicsWorld()->GetLinearVelocity(box)) < 0.1f);
	scene.OnRuntimeStop();
}

TEST_CASE("Gravity setting and gravity factor")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity box = MakeBox(scene, { 0, 5, 0 });
	scene.OnRuntimeStart();
	Simulate(scene, 1.0f);
	CHECK(box.Transform().Translation.y == doctest::Approx(5.0f).epsilon(0.001));
	scene.GetPhysicsWorld()->SetGravity({ 0, -10, 0 });
	Simulate(scene, 0.5f);
	CHECK(box.Transform().Translation.y < 5.0f);
	scene.OnRuntimeStop();
}

TEST_CASE("Velocity, impulse and force control")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity box = MakeBox(scene, { 0, 0, 0 });
	box.GetComponent<RigidBodyComponent>().LinearDamping = 0.0f;
	scene.OnRuntimeStart();
	PhysicsWorld* physics = scene.GetPhysicsWorld();

	physics->SetLinearVelocity(box, { 2, 0, 0 });
	CHECK(physics->GetLinearVelocity(box).x == doctest::Approx(2.0f));
	Simulate(scene, 1.0f);
	CHECK(box.Transform().Translation.x == doctest::Approx(2.0f).epsilon(0.05));

	physics->SetLinearVelocity(box, { 0, 0, 0 });
	physics->AddImpulse(box, { 0, 3, 0 }); // mass 1 -> v = 3
	CHECK(physics->GetLinearVelocity(box).y == doctest::Approx(3.0f).epsilon(0.01));

	physics->SetLinearVelocity(box, { 0, 0, 0 });
	physics->AddForce(box, { 0, 0, 60 }); // 1 kg at 60 N for one 1/60 step -> 1 m/s
	scene.OnUpdateRuntime(1.0f / 60.0f);
	CHECK(physics->GetLinearVelocity(box).z == doctest::Approx(1.0f).epsilon(0.1));

	physics->SetAngularVelocity(box, { 0, 1, 0 });
	CHECK(physics->GetAngularVelocity(box).y == doctest::Approx(1.0f));

	// Entities without a body are ignored.
	Entity plain = scene.CreateEntity("Plain");
	physics->AddForce(plain, { 1, 1, 1 });
	CHECK(physics->GetLinearVelocity(plain) == glm::vec3(0));
	CHECK_FALSE(physics->HasBody(plain));
	scene.OnRuntimeStop();
}

TEST_CASE("Raycast and overlap queries")
{
	Scene scene;
	MakeFloor(scene);
	Entity box = MakeBox(scene, { 0, 3, 0 });
	box.GetComponent<RigidBodyComponent>().Type = BodyType::Static;
	scene.OnRuntimeStart();
	PhysicsWorld* physics = scene.GetPhysicsWorld();

	RaycastHit hit;
	REQUIRE(physics->Raycast({ 0, 10, 0 }, { 0, -1, 0 }, 100.0f, hit));
	CHECK(hit.HitEntity == box.GetUUID());
	CHECK(hit.Distance == doctest::Approx(6.5f).epsilon(0.01));
	CHECK(hit.Normal.y == doctest::Approx(1.0f).epsilon(0.01));

	REQUIRE(physics->Raycast({ 10, 10, 0 }, { 0, -1, 0 }, 100.0f, hit));
	CHECK(scene.FindEntityByUUID(hit.HitEntity).GetName() == "Floor");
	CHECK_FALSE(physics->Raycast({ 10, 10, 0 }, { 0, 1, 0 }, 100.0f, hit));
	CHECK_FALSE(physics->Raycast({ 10, 10, 0 }, { 0, -1, 0 }, 5.0f, hit)); // too short
	CHECK_FALSE(physics->Raycast({ 0, 10, 0 }, { 0, 0, 0 }, 100.0f, hit)); // zero direction

	auto overlap = physics->OverlapSphere({ 0, 3, 0 }, 0.6f);
	CHECK(overlap.size() == 1);
	CHECK(overlap[0] == box.GetUUID());
	CHECK(physics->OverlapSphere({ 0, 20, 0 }, 1.0f).empty());
	scene.OnRuntimeStop();
}

TEST_CASE("Collider shapes: sphere rolls, capsule stands, offsets and scale apply")
{
	Scene scene;
	MakeFloor(scene);
	Entity sphere = scene.CreateEntity("Sphere");
	sphere.Transform().Translation = { 0, 3, 0 };
	sphere.AddComponent<RigidBodyComponent>();
	auto& sc = sphere.AddComponent<ColliderComponent>();
	sc.Shape = ColliderShape::Sphere;
	sc.Size = { 1, 1, 1 };

	Entity capsule = scene.CreateEntity("Capsule");
	capsule.Transform().Translation = { 5, 3, 0 };
	capsule.AddComponent<RigidBodyComponent>();
	auto& cc = capsule.AddComponent<ColliderComponent>();
	cc.Shape = ColliderShape::Capsule;
	cc.Size = { 1, 2, 1 };

	Entity scaled = scene.CreateEntity("Scaled");
	scaled.Transform().Translation = { -5, 5, 0 };
	scaled.Transform().Scale = { 2, 2, 2 }; // collider is 1x1x1 -> becomes 2x2x2
	scaled.AddComponent<RigidBodyComponent>();
	scaled.AddComponent<ColliderComponent>();

	scene.OnRuntimeStart();
	Simulate(scene, 4.0f);
	CHECK(sphere.Transform().Translation.y == doctest::Approx(0.5f).epsilon(0.05));
	CHECK(capsule.Transform().Translation.y == doctest::Approx(1.0f).epsilon(0.05));
	CHECK(scaled.Transform().Translation.y == doctest::Approx(1.0f).epsilon(0.05));
	CHECK(scaled.Transform().Scale.x == 2.0f); // scale survives physics sync
	scene.OnRuntimeStop();
}

TEST_CASE("Rotation locks keep a body upright")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity box = MakeBox(scene, { 0, 0, 0 });
	auto& rb = box.GetComponent<RigidBodyComponent>();
	rb.LockRotationX = rb.LockRotationY = rb.LockRotationZ = true;
	scene.OnRuntimeStart();
	scene.GetPhysicsWorld()->SetAngularVelocity(box, { 5, 5, 5 });
	Simulate(scene, 1.0f);
	CHECK(glm::length(box.Transform().Rotation) < 0.01f);
	scene.OnRuntimeStop();
}

TEST_CASE("Kinematic body follows its transform and pushes dynamic bodies")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity mover = MakeBox(scene, { 0, 0, 0 }, "Mover");
	mover.GetComponent<RigidBodyComponent>().Type = BodyType::Kinematic;
	Entity target = MakeBox(scene, { 3, 0, 0 }, "Target");
	target.GetComponent<RigidBodyComponent>().LinearDamping = 0.0f;
	scene.OnRuntimeStart();
	for(int i = 0; i < 120; i++)
	{
		mover.Transform().Translation.x += 0.05f; // 3 m/s
		scene.OnUpdateRuntime(1.0f / 60.0f);
	}
	CHECK(mover.Transform().Translation.x == doctest::Approx(6.0f).epsilon(0.01));
	CHECK(target.Transform().Translation.x > 3.5f); // got pushed
	scene.OnRuntimeStop();
}

TEST_CASE("Triggers report enter/exit events and do not collide")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity trigger = scene.CreateEntity("Trigger");
	trigger.AddComponent<ColliderComponent>().IsTrigger = true;
	trigger.GetComponent<ColliderComponent>().Size = { 4, 4, 4 };
	Entity ball = MakeBox(scene, { 10, 0, 0 }, "Ball");
	scene.OnRuntimeStart();
	PhysicsWorld* physics = scene.GetPhysicsWorld();
	physics->SetLinearVelocity(ball, { -5, 0, 0 });

	bool entered = false, exited = false;
	for(int i = 0; i < 240; i++)
	{
		scene.OnUpdateRuntime(1.0f / 60.0f);
		for(const CollisionEvent& e : physics->GetEvents())
		{
			if(e.Type == CollisionEventType::TriggerBegin)
				entered = true;
			if(e.Type == CollisionEventType::TriggerEnd)
				exited = true;
		}
	}
	CHECK(entered);
	CHECK(exited);
	CHECK(ball.Transform().Translation.x < -5.0f); // passed straight through
	scene.OnRuntimeStop();
}

TEST_CASE("Collision events fire for solid contacts")
{
	Scene scene;
	MakeFloor(scene);
	MakeBox(scene, { 0, 2, 0 });
	scene.OnRuntimeStart();
	bool began = false;
	for(int i = 0; i < 120 && !began; i++)
	{
		scene.OnUpdateRuntime(1.0f / 60.0f);
		for(const CollisionEvent& e : scene.GetPhysicsWorld()->GetEvents())
			began |= e.Type == CollisionEventType::CollisionBegin;
	}
	CHECK(began);
	scene.OnRuntimeStop();
}

TEST_CASE("Bodies are created lazily for entities spawned at runtime and removed on destroy")
{
	Scene scene;
	scene.OnRuntimeStart();
	PhysicsWorld* physics = scene.GetPhysicsWorld();
	CHECK(physics->GetBodyCount() == 0);
	Entity box = MakeBox(scene, { 0, 5, 0 });
	scene.OnUpdateRuntime(1.0f / 60.0f);
	CHECK(physics->GetBodyCount() == 1);
	scene.DestroyEntity(box);
	CHECK(physics->GetBodyCount() == 0);
	scene.OnUpdateRuntime(1.0f / 60.0f); // must not crash with stale body
	scene.OnRuntimeStop();
}

TEST_CASE("RigidBody without collider creates no body and does not crash")
{
	Scene scene;
	Entity e = scene.CreateEntity("NoCollider");
	e.AddComponent<RigidBodyComponent>();
	scene.OnRuntimeStart();
	CHECK(scene.GetPhysicsWorld()->GetBodyCount() == 0);
	Simulate(scene, 0.2f);
	scene.OnRuntimeStop();
}

TEST_CASE("Teleport moves a body to the entity transform and clears velocity")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity box = MakeBox(scene, { 0, 0, 0 });
	scene.OnRuntimeStart();
	PhysicsWorld* physics = scene.GetPhysicsWorld();
	physics->SetLinearVelocity(box, { 1, 0, 0 });
	box.Transform().Translation = { 10, 10, 10 };
	physics->Teleport(box);
	CHECK(glm::length(physics->GetLinearVelocity(box)) == doctest::Approx(0.0f));
	Simulate(scene, 0.5f);
	CHECK(box.Transform().Translation.y == doctest::Approx(10.0f).epsilon(0.01));
	scene.OnRuntimeStop();
}

TEST_CASE("Child physics bodies write back local transforms correctly")
{
	Scene scene;
	scene.GetSettings().Gravity = { 0, 0, 0 };
	Entity parent = scene.CreateEntity("Parent");
	parent.Transform().Translation = { 100, 0, 0 };
	Entity child = MakeBox(scene, { 1, 0, 0 });
	child.SetParent(parent, false);
	scene.OnRuntimeStart();
	scene.GetPhysicsWorld()->SetLinearVelocity(child, { 0, 2, 0 });
	Simulate(scene, 1.0f);
	CHECK(child.Transform().Translation.x == doctest::Approx(1.0f).epsilon(0.01));
	CHECK(child.Transform().Translation.y > 1.5f);
	scene.OnRuntimeStop();
}

TEST_CASE("Pause and single-step")
{
	Scene scene;
	Entity box = MakeBox(scene, { 0, 5, 0 });
	scene.OnRuntimeStart();
	scene.SetPaused(true);
	Simulate(scene, 0.5f);
	CHECK(box.Transform().Translation.y == 5.0f);
	scene.Step(3);
	for(int i = 0; i < 10; i++)
		scene.OnUpdateRuntime(1.0f / 60.0f);
	CHECK(scene.GetFrameCount() == 3);
	CHECK(box.Transform().Translation.y < 5.0f);
	scene.OnRuntimeStop();
}

TEST_CASE("Starting and stopping runtime repeatedly is safe")
{
	Scene scene;
	MakeFloor(scene);
	MakeBox(scene, { 0, 2, 0 });
	for(int i = 0; i < 3; i++)
	{
		scene.OnRuntimeStart();
		Simulate(scene, 0.2f);
		scene.OnRuntimeStop();
		CHECK_FALSE(scene.IsRunning());
		CHECK(scene.GetPhysicsWorld() == nullptr);
	}
}
