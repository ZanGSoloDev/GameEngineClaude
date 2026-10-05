#include <doctest/doctest.h>

#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Platform/Input.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Project/ProjectExporter.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"
#include "Starfall/Scripting/ScriptWorld.h"

#include "TestUtils.h"
#include "Starfall/Core/FileSystem.h"

using namespace Starfall;

namespace {
	std::filesystem::path TestProjectFile() { return std::filesystem::path(STARFALL_TEST_PROJECT_DIR) / "TestProject.sfproj"; }
}

TEST_CASE("Feature test project: scene loads and contains every component type")
{
	REQUIRE(Project::Load(TestProjectFile()));
	Scene scene;
	REQUIRE(SceneSerializer::Deserialize(scene, Project::ResolvePath(Project::GetStartScene())));
	CHECK(scene.GetEntityCount() > 30);

	bool seen[9] = {};
	scene.Each<TransformComponent>([&](Entity e, TransformComponent&) {
		seen[0] = true;
		seen[1] |= e.HasComponent<CameraComponent>();
		seen[2] |= e.HasComponent<MeshRendererComponent>();
		seen[3] |= e.HasComponent<LightComponent>();
		seen[4] |= e.HasComponent<RigidBodyComponent>();
		seen[5] |= e.HasComponent<ColliderComponent>();
		seen[6] |= e.HasComponent<AudioSourceComponent>();
		seen[7] |= e.HasComponent<AudioListenerComponent>();
		seen[8] |= e.HasComponent<ScriptComponent>();
	});
	for(int i = 0; i < 9; i++)
		CHECK_MESSAGE(seen[i], "component index " << i);

	// All three light types, all three body types and all collider shapes appear.
	int lightTypes = 0, bodyTypes = 0, shapes = 0;
	bool l[3] = {}, b[3] = {}, s[3] = {};
	scene.Each<LightComponent>([&](Entity, LightComponent& c) { l[int(c.Type)] = true; });
	scene.Each<RigidBodyComponent>([&](Entity, RigidBodyComponent& c) { b[int(c.Type)] = true; });
	scene.Each<ColliderComponent>([&](Entity, ColliderComponent& c) { s[int(c.Shape)] = true; });
	for(int i = 0; i < 3; i++) { lightTypes += l[i]; bodyTypes += b[i]; shapes += s[i]; }
	CHECK(lightTypes == 3);
	CHECK(bodyTypes == 3);
	CHECK(shapes == 3);

	// Every referenced asset resolves.
	AssetManager::Clear();
	scene.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr) {
		CHECK_MESSAGE(AssetManager::GetMesh(mr.Mesh) != nullptr, e.GetName());
		if(!mr.Material.empty())
			CHECK_MESSAGE(AssetManager::GetMaterial(mr.Material) != nullptr, mr.Material);
	});
	CHECK(AssetManager::GetHDRI(scene.GetSettings().Environment.HDRI) != nullptr);
	AssetManager::Clear();
	Project::Unload();
}

TEST_CASE("Feature test project: runs headless and the Lua API self test passes")
{
	Input::Reset();
	REQUIRE(Project::Load(TestProjectFile()));
	AudioEngine::Init(true);

	Scene scene;
	REQUIRE(SceneSerializer::Deserialize(scene, Project::ResolvePath(Project::GetStartScene())));
	scene.OnRuntimeStart();
	for(int i = 0; i < 400; i++)
		scene.OnUpdateRuntime(1.0f / 60.0f);

	ScriptWorld* scripts = scene.GetScriptWorld();
	REQUIRE(scripts);
	INFO("script errors: " << (scripts->GetErrors().empty() ? "none" : scripts->GetErrors().front()));
	CHECK(scripts->GetGlobalNumber("SelfTestDone") == 1);
	CHECK(scripts->GetGlobalNumber("SelfTestFailures", -1) == 0);
	CHECK(scripts->GetGlobalNumber("SelfTestChecks") > 60);
	// Only the deliberate missing-prefab probe may have logged a script-side error; engine errors list must stay empty.
	CHECK(scripts->GetErrors().empty());
	CHECK(scripts->GetGlobalNumber("BallHits") >= 1);
	CHECK(scripts->GetGlobalNumber("TriggerEnters") >= 1);

	scene.OnRuntimeStop();
	AudioEngine::Shutdown();
	AssetManager::Clear();
	Project::Unload();
}

TEST_CASE("Exporting never deletes an unrelated Assets folder")
{
	TestProject scratch;
	std::filesystem::path out = scratch.Root() / "Export";
	FileSystem::WriteText(out / "Assets" / "KeepMe.txt", "user data");
	FileSystem::WriteText(scratch.Root() / "Runtime.exe", "binary");
	FileSystem::WriteText(scratch.Root() / "Resources" / "Shaders" / "x.spv", "spv");
	REQUIRE(ExportProject(TestProjectFile(), out, scratch.Root() / "Runtime.exe", scratch.Root() / "Resources").Success);
	CHECK(std::filesystem::exists(out / "Assets" / "KeepMe.txt"));
}

TEST_CASE("Exporting the feature test project produces a runnable game folder layout")
{
	TestProject scratch; // only used for a unique temp directory
	std::filesystem::path out = scratch.Root() / "Export";
	std::filesystem::path fakeRuntime = scratch.Root() / "Runtime.exe";
	FileSystem::WriteText(fakeRuntime, "binary");
	std::filesystem::path resources = scratch.Root() / "Resources";
	FileSystem::WriteText(resources / "Shaders" / "x.spv", "spv");

	ExportResult result = ExportProject(TestProjectFile(), out, fakeRuntime, resources);
	INFO(result.Message);
	REQUIRE(result.Success);
	CHECK(std::filesystem::exists(out / "TestProject.exe"));
	CHECK(std::filesystem::exists(out / "Game.sfproj"));
	CHECK(std::filesystem::exists(out / "Resources" / "Shaders" / "x.spv"));
	CHECK(std::filesystem::exists(out / "Assets" / "Scenes" / "Main.sfscene"));
	CHECK(std::filesystem::exists(out / "Assets" / "Models" / "Cube.gltf"));

	// The exported project loads on its own.
	REQUIRE(Project::Load(out / "Game.sfproj"));
	Scene scene;
	CHECK(SceneSerializer::Deserialize(scene, Project::ResolvePath(Project::GetStartScene())));
	Project::Unload();

	// Failure cases
	CHECK_FALSE(ExportProject(scratch.Root() / "none.sfproj", out, fakeRuntime, resources).Success);
	CHECK_FALSE(ExportProject(TestProjectFile(), out, scratch.Root() / "missing.exe", resources).Success);
	CHECK_FALSE(ExportProject(TestProjectFile(), out, fakeRuntime, scratch.Root() / "NoResources").Success);
	CHECK_FALSE(ExportProject(TestProjectFile(), TestProjectFile().parent_path() / "Inside", fakeRuntime, resources).Success);
}
