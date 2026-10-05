#include <doctest/doctest.h>

#include "Starfall/Math/MathUtils.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"

#include "TestUtils.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Starfall;

TEST_CASE("Entity creation has default components")
{
	Scene scene;
	Entity e = scene.CreateEntity("Hero");
	CHECK(e);
	CHECK(e.GetName() == "Hero");
	CHECK(e.HasComponent<TransformComponent>());
	CHECK(e.HasComponent<RelationshipComponent>());
	CHECK_FALSE(e.GetUUID().IsNull());
	CHECK(scene.FindEntityByUUID(e.GetUUID()) == e);
	CHECK(scene.FindEntityByName("Hero") == e);
	CHECK_FALSE(scene.FindEntityByName("Nobody"));
	CHECK(scene.CreateEntity("").GetName() == "Entity");
}

TEST_CASE("Entity destroy removes it and descendants")
{
	Scene scene;
	Entity parent = scene.CreateEntity("P");
	Entity child = scene.CreateEntity("C");
	Entity grandchild = scene.CreateEntity("G");
	child.SetParent(parent);
	grandchild.SetParent(child);
	UUID grandId = grandchild.GetUUID();
	CHECK(scene.GetEntityCount() == 3);
	scene.DestroyEntity(parent);
	CHECK(scene.GetEntityCount() == 0);
	CHECK_FALSE(scene.FindEntityByUUID(grandId));
	CHECK_FALSE(parent);
	CHECK_FALSE(child);
}

TEST_CASE("Destroying a child detaches it from the parent")
{
	Scene scene;
	Entity parent = scene.CreateEntity("P");
	Entity a = scene.CreateEntity("A");
	Entity b = scene.CreateEntity("B");
	a.SetParent(parent);
	b.SetParent(parent);
	scene.DestroyEntity(a);
	CHECK(parent.GetChildren().size() == 1);
	CHECK(parent.GetChildren()[0] == b);
}

TEST_CASE("QueueDestroy is deferred and tolerates duplicates and children")
{
	Scene scene;
	Entity parent = scene.CreateEntity("P");
	Entity child = scene.CreateEntity("C");
	child.SetParent(parent);
	scene.QueueDestroy(child);
	scene.QueueDestroy(parent);
	scene.QueueDestroy(child);
	CHECK(scene.GetEntityCount() == 2);
	scene.OnRuntimeStart();
	scene.OnUpdateRuntime(0.016f);
	scene.OnRuntimeStop();
	CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("Hierarchy world transforms and keep-world reparenting")
{
	Scene scene;
	Entity parent = scene.CreateEntity("P");
	parent.Transform().Translation = { 10, 0, 0 };
	parent.Transform().Scale = { 2, 2, 2 };
	Entity child = scene.CreateEntity("C");
	child.Transform().Translation = { 1, 0, 0 };
	child.SetParent(parent, false);
	glm::vec3 world(scene.GetWorldTransform(child)[3]);
	CHECK(world.x == doctest::Approx(12.0f));

	// Reparent preserving world position.
	child.SetParent(Entity(), true);
	CHECK(child.Transform().Translation.x == doctest::Approx(12.0f));
	CHECK_FALSE(child.GetParent());

	child.SetParent(parent, true);
	CHECK(child.Transform().Translation.x == doctest::Approx(1.0f));
}

TEST_CASE("Parent cycles are rejected")
{
	Scene scene;
	Entity a = scene.CreateEntity("A");
	Entity b = scene.CreateEntity("B");
	b.SetParent(a);
	a.SetParent(b);       // would create a cycle
	CHECK_FALSE(a.GetParent());
	a.SetParent(a);       // self parent
	CHECK_FALSE(a.GetParent());
	CHECK(scene.IsDescendantOf(b, a));
}

TEST_CASE("DuplicateEntity clones subtree with new UUIDs")
{
	Scene scene;
	Entity root = scene.CreateEntity("Root");
	Entity child = scene.CreateEntity("Child");
	child.SetParent(root);
	child.AddComponent<LightComponent>().Intensity = 7.0f;
	Entity copy = scene.DuplicateEntity(root);
	CHECK(copy != root);
	CHECK(copy.GetUUID() != root.GetUUID());
	REQUIRE(copy.GetChildren().size() == 1);
	Entity childCopy = copy.GetChildren()[0];
	CHECK(childCopy.GetUUID() != child.GetUUID());
	CHECK(childCopy.GetComponent<LightComponent>().Intensity == 7.0f);
	CHECK(scene.GetEntityCount() == 4);
	CHECK(childCopy.GetParent() == copy);
}

TEST_CASE("Scene::Copy deep copies and resets runtime state")
{
	Scene scene;
	scene.GetSettings().PostProcess.Exposure = 3.0f;
	Entity e = scene.CreateEntity("Body");
	e.AddComponent<RigidBodyComponent>().RuntimeBodyID = 5;
	Ref<Scene> copy = Scene::Copy(scene);
	Entity c = copy->FindEntityByUUID(e.GetUUID());
	REQUIRE(c);
	CHECK(c.GetName() == "Body");
	CHECK(c.GetComponent<RigidBodyComponent>().RuntimeBodyID == 0xFFFFFFFFu);
	CHECK(copy->GetSettings().PostProcess.Exposure == 3.0f);
	copy->DestroyEntity(c);
	CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("Primary camera lookup")
{
	Scene scene;
	CHECK_FALSE(scene.GetPrimaryCameraEntity());
	Entity cam = scene.CreateEntity("Cam");
	cam.AddComponent<CameraComponent>();
	CHECK(scene.GetPrimaryCameraEntity() == cam);
	cam.GetComponent<CameraComponent>().Primary = false;
	CHECK_FALSE(scene.GetPrimaryCameraEntity());
}

TEST_CASE("Camera projection is Y-up with zero-to-one depth")
{
	CameraComponent camera;
	glm::mat4 p = camera.GetProjection(16.0f / 9.0f);
	CHECK(p[1][1] > 0.0f);
	glm::vec4 nearPoint = p * glm::vec4(0, 0, -camera.PerspectiveNear, 1);
	glm::vec4 farPoint = p * glm::vec4(0, 0, -camera.PerspectiveFar, 1);
	CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f).epsilon(1e-3));
	CHECK(farPoint.z / farPoint.w == doctest::Approx(1.0f).epsilon(1e-3));
	camera.Projection = ProjectionType::Orthographic;
	CHECK(camera.GetProjection(1.0f)[1][1] > 0.0f);
}

TEST_CASE("Math: decompose, AABB, rays and frustum")
{
	glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) * glm::scale(glm::mat4(1.0f), glm::vec3(2, 2, 2));
	glm::vec3 t, r, s;
	CHECK(Math::DecomposeTransform(m, t, r, s));
	CHECK(t.y == doctest::Approx(2.0f));
	CHECK(s.x == doctest::Approx(2.0f));

	Math::AABB box;
	CHECK_FALSE(box.IsValid());
	box.Expand(glm::vec3(-1));
	box.Expand(glm::vec3(1));
	CHECK(box.IsValid());
	CHECK(box.Center() == glm::vec3(0));

	float tHit = 0;
	CHECK(Math::IntersectRayAABB({ { 0, 0, 5 }, { 0, 0, -1 } }, box, tHit));
	CHECK(tHit == doctest::Approx(4.0f));
	CHECK_FALSE(Math::IntersectRayAABB({ { 5, 5, 5 }, { 0, 0, -1 } }, box, tHit));
	CHECK_FALSE(Math::IntersectRayAABB({ { 0, 0, 5 }, { 0, 0, 1 } }, box, tHit)); // pointing away

	CHECK(Math::IntersectRayTriangle({ { 0.25f, 0.25f, 1 }, { 0, 0, -1 } }, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, tHit));
	CHECK(tHit == doctest::Approx(1.0f));
	CHECK_FALSE(Math::IntersectRayTriangle({ { 2, 2, 1 }, { 0, 0, -1 } }, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, tHit));

	glm::mat4 vp = CameraComponent().GetProjection(1.0f) * glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0));
	Math::Frustum frustum = Math::Frustum::FromViewProjection(vp);
	Math::AABB visible{ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
	Math::AABB behind{ { -0.5f, -0.5f, 9.5f }, { 0.5f, 0.5f, 10.5f } };
	Math::AABB far{ { -0.5f, -0.5f, -1000.5f }, { 0.5f, 0.5f, -999.5f } };
	Math::AABB side{ { 100, 0, 0 }, { 101, 1, 1 } };
	CHECK(frustum.Intersects(visible));
	CHECK_FALSE(frustum.Intersects(behind));
	CHECK_FALSE(frustum.Intersects(far));
	CHECK_FALSE(frustum.Intersects(side));
}

TEST_CASE("Serializer round trip preserves everything")
{
	Scene scene;
	scene.SetName("RoundTrip");
	scene.GetSettings().Environment.HDRI = "HDRI/sky.hdr";
	scene.GetSettings().PostProcess.TonemapMode = Tonemapper::AgX;
	scene.GetSettings().Gravity = { 0, -3, 0 };

	Entity parent = scene.CreateEntity("Parent");
	parent.Transform().Translation = { 1, 2, 3 };
	parent.Transform().Rotation = { 0.1f, 0.2f, 0.3f };
	auto& mr = parent.AddComponent<MeshRendererComponent>();
	mr.Mesh = "Models/A.gltf#2";
	mr.CastShadows = false;
	Entity child = scene.CreateEntity("Child");
	child.SetParent(parent, false);
	child.AddComponent<LightComponent>() = LightComponent{ LightType::Spot, { 1, 0.5f, 0.25f }, 3.0f, 9.0f, 0.2f, 0.4f, true, 2.0f, 0.01f };
	auto& rb = child.AddComponent<RigidBodyComponent>();
	rb.Type = BodyType::Kinematic;
	rb.LockRotationY = true;
	auto& col = child.AddComponent<ColliderComponent>();
	col.Shape = ColliderShape::Capsule;
	col.IsTrigger = true;
	col.Size = { 0.5f, 2.0f, 0.5f };
	auto& audio = child.AddComponent<AudioSourceComponent>();
	audio.Clip = "Audio/a.wav";
	audio.Loop = true;
	child.AddComponent<AudioListenerComponent>().Active = false;
	auto& script = child.AddComponent<ScriptComponent>();
	script.Script = "Scripts/S.lua";
	script.Properties["speed"] = 2.5;
	script.Properties["flag"] = true;
	script.Properties["label"] = std::string("hi");
	child.AddComponent<CameraComponent>().Projection = ProjectionType::Orthographic;

	std::string text = SceneSerializer::SerializeToString(scene);
	Scene loaded;
	REQUIRE(SceneSerializer::DeserializeFromString(loaded, text));

	CHECK(loaded.GetName() == "RoundTrip");
	CHECK(loaded.GetSettings().Environment.HDRI == "HDRI/sky.hdr");
	CHECK(loaded.GetSettings().PostProcess.TonemapMode == Tonemapper::AgX);
	CHECK(loaded.GetSettings().Gravity.y == -3.0f);
	CHECK(loaded.GetEntityCount() == 2);

	Entity lp = loaded.FindEntityByUUID(parent.GetUUID());
	Entity lc = loaded.FindEntityByUUID(child.GetUUID());
	REQUIRE(lp);
	REQUIRE(lc);
	CHECK(lc.GetParent() == lp);
	CHECK(lp.Transform().Translation == glm::vec3(1, 2, 3));
	CHECK(lp.Transform().Rotation.z == doctest::Approx(0.3f));
	CHECK(lp.GetComponent<MeshRendererComponent>().Mesh == "Models/A.gltf#2");
	CHECK_FALSE(lp.GetComponent<MeshRendererComponent>().CastShadows);
	const auto& l = lc.GetComponent<LightComponent>();
	CHECK(l.Type == LightType::Spot);
	CHECK(l.Color.y == 0.5f);
	CHECK(l.OuterConeAngle == doctest::Approx(0.4f));
	CHECK(l.ShadowSoftness == 2.0f);
	CHECK(lc.GetComponent<RigidBodyComponent>().Type == BodyType::Kinematic);
	CHECK(lc.GetComponent<RigidBodyComponent>().LockRotationY);
	CHECK(lc.GetComponent<ColliderComponent>().Shape == ColliderShape::Capsule);
	CHECK(lc.GetComponent<ColliderComponent>().IsTrigger);
	CHECK(lc.GetComponent<AudioSourceComponent>().Loop);
	CHECK_FALSE(lc.GetComponent<AudioListenerComponent>().Active);
	const auto& s = lc.GetComponent<ScriptComponent>();
	CHECK(s.Script == "Scripts/S.lua");
	CHECK(std::get<double>(s.Properties.at("speed")) == 2.5);
	CHECK(std::get<bool>(s.Properties.at("flag")));
	CHECK(std::get<std::string>(s.Properties.at("label")) == "hi");
	CHECK(lc.GetComponent<CameraComponent>().Projection == ProjectionType::Orthographic);

	// Serializing the loaded scene again must be stable.
	CHECK(SceneSerializer::SerializeToString(loaded).size() == text.size());
}

TEST_CASE("Serializer rejects malformed input without crashing")
{
	Scene scene;
	scene.CreateEntity("Keep");
	CHECK_FALSE(SceneSerializer::DeserializeFromString(scene, ""));
	CHECK_FALSE(SceneSerializer::DeserializeFromString(scene, "not json"));
	CHECK_FALSE(SceneSerializer::DeserializeFromString(scene, "[1,2,3]"));
	CHECK_FALSE(SceneSerializer::DeserializeFromString(scene, "{\"Type\":\"Prefab\",\"Entities\":[]}"));
	CHECK_FALSE(SceneSerializer::DeserializeFromString(scene, "{\"Type\":\"Scene\",\"Version\":999,\"Entities\":[]}"));
	CHECK(scene.GetEntityCount() == 1); // scene untouched on failure

	// Garbage inside valid structure is skipped or defaulted.
	std::string odd = R"({"Type":"Scene","Version":1,"Entities":[
		1, "x", {"UUID":"nope"}, {"UUID":0}, {"UUID":5,"Name":"Ok","Parent":99,"Components":{"TransformComponent":{"Translation":[1,2]},"LightComponent":{"Type":99}}},
		{"UUID":5,"Name":"Dup"}]})";
	REQUIRE(SceneSerializer::DeserializeFromString(scene, odd));
	CHECK(scene.GetEntityCount() == 1);
	Entity e = scene.FindEntityByUUID(UUID(5));
	REQUIRE(e);
	CHECK(e.GetName() == "Ok");
	CHECK(e.Transform().Translation == glm::vec3(0)); // malformed vector keeps default
	CHECK(e.GetComponent<LightComponent>().Type == LightType::Point); // out of range enum keeps default
	CHECK_FALSE(e.GetParent());
}

TEST_CASE("Scene file save/load and prefab instantiation")
{
	TestProject project;
	Scene scene;
	Entity root = scene.CreateEntity("Bullet");
	root.AddComponent<MeshRendererComponent>().Mesh = "builtin://Sphere";
	Entity trail = scene.CreateEntity("Trail");
	trail.SetParent(root, false);
	trail.Transform().Translation = { 0, 0, 1 };

	REQUIRE(SceneSerializer::SerializePrefab(scene, root, project.Assets() / "Prefabs" / "Bullet.sfprefab"));
	REQUIRE(SceneSerializer::Serialize(scene, project.Assets() / "Scenes" / "S.sfscene"));

	Scene runtime;
	glm::vec3 pos(4, 5, 6);
	Entity a = runtime.InstantiatePrefab("Prefabs/Bullet.sfprefab", &pos, Entity());
	Entity b = runtime.InstantiatePrefab("Prefabs/Bullet.sfprefab");
	REQUIRE(a);
	REQUIRE(b);
	CHECK(runtime.GetEntityCount() == 4);
	CHECK(a.Transform().Translation == pos);
	CHECK(a.GetUUID() != b.GetUUID());
	CHECK(a.GetUUID() != root.GetUUID());
	REQUIRE(a.GetChildren().size() == 1);
	CHECK(a.GetChildren()[0].GetName() == "Trail");
	CHECK(a.GetChildren()[0].GetParent() == a);

	// Missing and path-traversing prefabs fail cleanly.
	CHECK_FALSE(runtime.InstantiatePrefab("Prefabs/Missing.sfprefab"));
	CHECK_FALSE(runtime.InstantiatePrefab("../Secret.sfprefab"));

	Scene loaded;
	REQUIRE(SceneSerializer::Deserialize(loaded, project.Assets() / "Scenes" / "S.sfscene"));
	CHECK(loaded.GetEntityCount() == 2);
	CHECK_FALSE(SceneSerializer::Deserialize(loaded, project.Assets() / "Scenes" / "Nope.sfscene"));
}

TEST_CASE("Project path validation")
{
	CHECK(Project::IsValidAssetPath("Models/a.gltf"));
	CHECK(Project::IsValidAssetPath("a.txt"));
	CHECK_FALSE(Project::IsValidAssetPath(""));
	CHECK_FALSE(Project::IsValidAssetPath("../x"));
	CHECK_FALSE(Project::IsValidAssetPath("a/../../x"));
	CHECK_FALSE(Project::IsValidAssetPath("/etc/passwd"));
	CHECK_FALSE(Project::IsValidAssetPath("C:/Windows/x"));
	CHECK(Project::IsValidAssetPath("a/../b.txt"));
}

TEST_CASE("Project create/load round trip")
{
	TestProject helper;
	std::filesystem::path dir = helper.Root() / "MyGame";
	REQUIRE(Project::Create(dir, "MyGame"));
	CHECK(Project::GetName() == "MyGame");
	CHECK(std::filesystem::exists(dir / "MyGame.sfproj"));
	CHECK(std::filesystem::exists(dir / "Assets" / "Scenes"));
	Project::SetStartScene("Scenes/Level1.sfscene");
	REQUIRE(Project::Save());
	Project::Unload();
	CHECK_FALSE(Project::IsLoaded());
	REQUIRE(Project::Load(dir / "MyGame.sfproj"));
	CHECK(Project::GetStartScene() == "Scenes/Level1.sfscene");
	CHECK(Project::GetAssetDirectory() == dir / "Assets");
	CHECK(Project::MakeAssetPath(dir / "Assets" / "Scenes" / "x.sfscene") == "Scenes/x.sfscene");
	CHECK(Project::MakeAssetPath(helper.Root() / "elsewhere.txt").empty());
	CHECK_FALSE(Project::Load(dir / "Missing.sfproj"));
}

TEST_CASE("UUIDs are unique and seedable")
{
	UUID::Seed(42);
	UUID a;
	UUID::Seed(42);
	UUID b;
	CHECK(a == b);
	UUID c;
	CHECK(c != b);
	CHECK_FALSE(a.IsNull());
}

TEST_CASE("FileSystem atomic write and read")
{
	TestProject project;
	std::filesystem::path file = project.Assets() / "Sub" / "f.txt";
	REQUIRE(FileSystem::WriteText(file, "hello"));
	REQUIRE(FileSystem::WriteText(file, "world")); // overwrite
	CHECK(*FileSystem::ReadText(file) == "world");
	CHECK_FALSE(std::filesystem::exists(file.string() + ".tmp"));
	CHECK_FALSE(FileSystem::ReadText(project.Assets() / "none.txt").has_value());
	std::vector<uint8_t> bytes{ 0, 1, 2, 255 };
	REQUIRE(FileSystem::WriteBinary(project.Assets() / "b.bin", bytes.data(), bytes.size()));
	CHECK(*FileSystem::ReadBinary(project.Assets() / "b.bin") == bytes);
}
