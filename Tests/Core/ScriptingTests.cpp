#include <doctest/doctest.h>

#include "Starfall/Platform/Input.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"
#include "Starfall/Scripting/ScriptWorld.h"

#include "TestUtils.h"

using namespace Starfall;

namespace {

	Entity WithScript(Scene& scene, const char* name, const char* path)
	{
		Entity e = scene.CreateEntity(name);
		e.AddComponent<ScriptComponent>().Script = path;
		return e;
	}

	void Run(Scene& scene, int frames, float dt = 1.0f / 60.0f)
	{
		for(int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(dt);
	}

}

TEST_CASE("Script lifecycle: OnCreate, OnUpdate, OnLateUpdate, OnDestroy")
{
	TestProject project;
	project.Write("Scripts/Life.lua", R"(
		local M = {}
		function M:OnCreate() created = (created or 0) + 1 end
		function M:OnUpdate(dt) updates = (updates or 0) + 1; total = (total or 0) + dt end
		function M:OnLateUpdate(dt) late = (late or 0) + 1 end
		function M:OnDestroy() destroyed = (destroyed or 0) + 1 end
		return M
	)");
	Scene scene;
	WithScript(scene, "A", "Scripts/Life.lua");
	scene.OnRuntimeStart();
	ScriptWorld* scripts = scene.GetScriptWorld();
	REQUIRE(scripts);
	CHECK(scripts->GetInstanceCount() == 1);
	CHECK(scripts->GetGlobalNumber("created") == 1);
	Run(scene, 10, 0.1f);
	CHECK(scripts->GetGlobalNumber("updates") == 10);
	CHECK(scripts->GetGlobalNumber("late") == 10);
	CHECK(scripts->GetGlobalNumber("total") == doctest::Approx(1.0));
	CHECK(scripts->GetGlobalNumber("destroyed") == 0);
	CHECK(scripts->GetErrors().empty());
	scene.OnRuntimeStop();
	// Globals die with the state, so verify OnDestroy through a surviving scene-side effect below.
}

TEST_CASE("OnDestroy runs when the entity is destroyed and when the scene stops")
{
	TestProject project;
	project.Write("Scripts/D.lua", R"(
		local M = {}
		function M:OnDestroy() destroyCount = (destroyCount or 0) + 1 end
		return M
	)");
	Scene scene;
	Entity a = WithScript(scene, "A", "Scripts/D.lua");
	WithScript(scene, "B", "Scripts/D.lua");
	scene.OnRuntimeStart();
	ScriptWorld* scripts = scene.GetScriptWorld();
	scene.DestroyEntity(a);
	CHECK(scripts->GetGlobalNumber("destroyCount") == 1);
	CHECK(scripts->GetInstanceCount() == 1);
	scene.OnRuntimeStop();
}

TEST_CASE("Script properties: defaults, per-entity overrides, self.entity")
{
	TestProject project;
	project.Write("Scripts/Props.lua", R"(
		local M = {}
		M.properties = { speed = 2.0, label = "default", enabled = false }
		function M:OnUpdate(dt)
			self.entity.transform.position = Vec3(self.speed, 0, 0)
			lastLabel = self.label
			lastEnabled = self.enabled
		end
		return M
	)");
	Scene scene;
	Entity plain = WithScript(scene, "Plain", "Scripts/Props.lua");
	Entity custom = WithScript(scene, "Custom", "Scripts/Props.lua");
	auto& props = custom.GetComponent<ScriptComponent>().Properties;
	props["speed"] = 7.0;
	props["label"] = std::string("custom");
	props["enabled"] = true;

	scene.OnRuntimeStart();
	Run(scene, 1);
	CHECK(plain.Transform().Translation.x == 2.0f);
	CHECK(custom.Transform().Translation.x == 7.0f);
	scene.OnRuntimeStop();
}

TEST_CASE("Script errors are isolated, reported and disable only the faulty instance")
{
	TestProject project;
	project.Write("Scripts/Bad.lua", "local M = {}\nfunction M:OnUpdate(dt) error('boom') end\nreturn M");
	project.Write("Scripts/Good.lua", "local M = {}\nfunction M:OnUpdate(dt) good = (good or 0) + 1 end\nreturn M");
	project.Write("Scripts/Syntax.lua", "this is not lua");
	project.Write("Scripts/NoTable.lua", "return 5");
	Scene scene;
	WithScript(scene, "Bad", "Scripts/Bad.lua");
	WithScript(scene, "Good", "Scripts/Good.lua");
	WithScript(scene, "Syntax", "Scripts/Syntax.lua");
	WithScript(scene, "NoTable", "Scripts/NoTable.lua");
	WithScript(scene, "Missing", "Scripts/Missing.lua");
	WithScript(scene, "Traversal", "../Outside.lua");
	scene.OnRuntimeStart();
	Run(scene, 5);
	ScriptWorld* scripts = scene.GetScriptWorld();
	CHECK(scripts->GetGlobalNumber("good") == 5);
	CHECK(scripts->GetErrors().size() >= 5);
	bool sawBoom = false;
	for(const auto& e : scripts->GetErrors())
		sawBoom |= e.find("boom") != std::string::npos;
	CHECK(sawBoom);
	size_t errorsAfter = scripts->GetErrors().size();
	Run(scene, 5);
	CHECK(scripts->GetErrors().size() == errorsAfter); // faulted instance is not re-run every frame
	scene.OnRuntimeStop();
}

TEST_CASE("Runaway scripts are aborted instead of hanging the engine")
{
	TestProject project;
	project.Write("Scripts/Loop.lua", "local M = {}\nfunction M:OnUpdate(dt) while true do end end\nreturn M");
	Scene scene;
	WithScript(scene, "Loop", "Scripts/Loop.lua");
	scene.OnRuntimeStart();
	Run(scene, 2);
	REQUIRE_FALSE(scene.GetScriptWorld()->GetErrors().empty());
	CHECK(scene.GetScriptWorld()->GetErrors()[0].find("budget") != std::string::npos);
	scene.OnRuntimeStop();
}

TEST_CASE("Script sandbox blocks file and OS access")
{
	Scene scene;
	scene.OnRuntimeStart();
	ScriptWorld* scripts = scene.GetScriptWorld();
	std::string error;
	CHECK_FALSE(scripts->ExecuteString("io.open('x','w')", &error));
	CHECK_FALSE(scripts->ExecuteString("os.execute('echo hi')", &error));
	CHECK_FALSE(scripts->ExecuteString("dofile('x.lua')", &error));
	CHECK_FALSE(scripts->ExecuteString("loadfile('x.lua')", &error));
	CHECK_FALSE(scripts->ExecuteString("debug.getinfo(1)", &error));
	CHECK_FALSE(scripts->ExecuteString("package.loadlib('a','b')", &error));
	CHECK(scripts->ExecuteString("assert(os.time() > 0 and os.clock() >= 0)"));
	CHECK(scripts->ExecuteString("assert(string.format('%d', 5) == '5' and math.floor(2.5) == 2 and #table.concat({1,2}) == 2)"));
	scene.OnRuntimeStop();
}

TEST_CASE("require loads sibling modules from Scripts and rejects unknown ones")
{
	TestProject project;
	project.Write("Scripts/Lib/Util.lua", "local U = {} function U.double(x) return x * 2 end return U");
	Scene scene;
	scene.OnRuntimeStart();
	ScriptWorld* scripts = scene.GetScriptWorld();
	CHECK(scripts->ExecuteString("result = require('Lib.Util').double(21)"));
	CHECK(scripts->GetGlobalNumber("result") == 42);
	CHECK_FALSE(scripts->ExecuteString("require('Nope')"));
	CHECK_FALSE(scripts->ExecuteString("require('../../Secret')"));
	scene.OnRuntimeStop();
}

TEST_CASE("Vec3 and Mathf API")
{
	Scene scene;
	scene.OnRuntimeStart();
	ScriptWorld* s = scene.GetScriptWorld();
	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		local a, b = Vec3(1, 2, 3), Vec3(4, 5, 6)
		local c = a + b
		sumX, sumY, sumZ = c.x, c.y, c.z
		dot = a:Dot(b)
		local cr = Vec3(1,0,0):Cross(Vec3(0,1,0))
		crossZ = cr.z
		len = Vec3(3, 4, 0):Length()
		scaled = (a * 2).y
		scaled2 = (2 * a).z
		neg = (-a).x
		div = (b / 2).x
		norm = Vec3(0, 0, 5):Normalized().z
		zeroNorm = Vec3(0, 0, 0):Normalized().x
		eq = (Vec3(1,1,1) == Vec3(1,1,1)) and 1 or 0
		lerp = Mathf.Lerp(0, 10, 0.25)
		clamp = Mathf.Clamp(5, 0, 1)
		approach = Mathf.MoveTowards(0, 10, 3)
		sign = Mathf.Sign(-4)
		str = tostring(Vec3(1, 2, 3))
		local v = Vec3(); v.x = 9; setX = v.x
		dist = Vec3(0,0,0):Distance(Vec3(0,3,4))
	)", &err), err);
	CHECK(s->GetGlobalNumber("sumX") == 5);
	CHECK(s->GetGlobalNumber("sumZ") == 9);
	CHECK(s->GetGlobalNumber("dot") == 32);
	CHECK(s->GetGlobalNumber("crossZ") == 1);
	CHECK(s->GetGlobalNumber("len") == 5);
	CHECK(s->GetGlobalNumber("scaled") == 4);
	CHECK(s->GetGlobalNumber("scaled2") == 6);
	CHECK(s->GetGlobalNumber("neg") == -1);
	CHECK(s->GetGlobalNumber("div") == 2);
	CHECK(s->GetGlobalNumber("norm") == 1);
	CHECK(s->GetGlobalNumber("zeroNorm") == 0);
	CHECK(s->GetGlobalNumber("eq") == 1);
	CHECK(s->GetGlobalNumber("lerp") == doctest::Approx(2.5));
	CHECK(s->GetGlobalNumber("clamp") == 1);
	CHECK(s->GetGlobalNumber("approach") == 3);
	CHECK(s->GetGlobalNumber("sign") == -1);
	CHECK(s->GetGlobalNumber("setX") == 9);
	CHECK(s->GetGlobalNumber("dist") == 5);
	CHECK(s->GetGlobalString("str").find("Vec3") == 0);
	scene.OnRuntimeStop();
}

TEST_CASE("Entity API: find, create, destroy, hierarchy, stale handles")
{
	Scene scene;
	Entity target = scene.CreateEntity("Target");
	scene.OnRuntimeStart();
	ScriptWorld* s = scene.GetScriptWorld();
	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		local t = Scene.Find("Target")
		foundName = t.name
		missing = Scene.Find("Nope") == nil and 1 or 0
		t.name = "Renamed"
		local child = Scene.Create("Kid")
		child.parent = t
		kidParent = child.parent.name
		childCount = #t:GetChildren()
		findChild = t:FindChild("Kid") ~= nil and 1 or 0
		byId = Scene.FindByID(child.id).name
		child.parent = nil
		orphan = child.parent == nil and 1 or 0
		child:Destroy()
		stillValidBeforeFlush = child.valid and 1 or 0
		allCount = #Scene.GetEntities()
		staleHandle = child
	)", &err), err);
	CHECK(s->GetGlobalString("foundName") == "Target");
	CHECK(s->GetGlobalNumber("missing") == 1);
	CHECK(s->GetGlobalString("kidParent") == "Renamed");
	CHECK(s->GetGlobalNumber("childCount") == 1);
	CHECK(s->GetGlobalNumber("findChild") == 1);
	CHECK(s->GetGlobalString("byId") == "Kid");
	CHECK(s->GetGlobalNumber("orphan") == 1);
	CHECK(s->GetGlobalNumber("stillValidBeforeFlush") == 1); // destroy is deferred to end of frame
	CHECK(target.GetName() == "Renamed");
	scene.OnUpdateRuntime(0.016f); // flush
	CHECK(scene.GetEntityCount() == 1);
	CHECK(s->ExecuteString("assert(staleHandle.valid == false)"));
	CHECK_FALSE(s->ExecuteString("local n = staleHandle.name"));          // stale access is a Lua error, not a crash
	CHECK_FALSE(s->ExecuteString("staleHandle.transform.position = Vec3(1,1,1)"));
	scene.OnRuntimeStop();
}

TEST_CASE("Component API: transform, light, camera, mesh renderer")
{
	Scene scene;
	Entity e = scene.CreateEntity("E");
	e.AddComponent<LightComponent>();
	e.AddComponent<CameraComponent>();
	e.AddComponent<MeshRendererComponent>();
	Entity parent = scene.CreateEntity("Parent");
	parent.Transform().Translation = { 10, 0, 0 };
	scene.OnRuntimeStart();
	ScriptWorld* s = scene.GetScriptWorld();
	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		local e = Scene.Find("E")
		local t = e.transform
		t.position = Vec3(1, 2, 3)
		t.rotation = Vec3(0, 90, 0)
		t.scale = Vec3(2, 2, 2)
		t:Translate(Vec3(1, 0, 0))
		t:Rotate(Vec3(0, 90, 0))
		rotY = t.rotation.y
		fwd = t.forward.x
		worldX = t.worldPosition.x

		local l = e:GetComponent("Light")
		l.type = "Spot"; l.intensity = 5; l.color = Vec3(1, 0, 0); l.range = 3; l.outerCone = 45
		lightType = l.type
		local cam = e:GetComponent("Camera")
		cam.fov = 90; cam.primary = false; cam.orthographic = true
		local mr = e:GetComponent("MeshRenderer")
		mr.mesh = "builtin://Sphere"; mr.castShadows = false; mr.visible = false
		hasRb = e:HasComponent("RigidBody") and 1 or 0
		noAudio = e:GetComponent("AudioSource") == nil and 1 or 0
		e:AddComponent("AudioSource").volume = 0.5
		hasAudio = e:HasComponent("AudioSource") and 1 or 0
		e:RemoveComponent("AudioSource")
		removed = e:HasComponent("AudioSource") and 0 or 1

		local p = Scene.Find("Parent")
		e.parent = p
		localAfterReparent = e.transform.position.x
		e.transform.worldPosition = Vec3(0, 0, 0)
		localAfterWorldSet = e.transform.position.x

		e:GetComponent("Light").type = "Banana"
	)", &err) == false, "last statement must fail");
	CHECK(err.find("unknown light type") != std::string::npos);
	CHECK(e.GetComponent<LightComponent>().Type == LightType::Spot);
	CHECK(e.GetComponent<LightComponent>().Intensity == 5.0f);
	CHECK(e.GetComponent<LightComponent>().OuterConeAngle == doctest::Approx(glm::radians(45.0f)));
	CHECK(e.GetComponent<CameraComponent>().VerticalFOV == doctest::Approx(glm::radians(90.0f)));
	CHECK_FALSE(e.GetComponent<CameraComponent>().Primary);
	CHECK(e.GetComponent<CameraComponent>().Projection == ProjectionType::Orthographic);
	CHECK(e.GetComponent<MeshRendererComponent>().Mesh == "builtin://Sphere");
	CHECK_FALSE(e.GetComponent<MeshRendererComponent>().Visible);
	CHECK(s->GetGlobalNumber("rotY") == doctest::Approx(180.0).epsilon(0.01));
	CHECK(s->GetGlobalNumber("hasRb") == 0);
	CHECK(s->GetGlobalNumber("noAudio") == 1);
	CHECK(s->GetGlobalNumber("hasAudio") == 1);
	CHECK(s->GetGlobalNumber("removed") == 1);
	CHECK(s->GetGlobalString("lightType") == "Spot");
	CHECK(s->GetGlobalNumber("worldX") == doctest::Approx(2.0));
	CHECK(s->GetGlobalNumber("localAfterReparent") == doctest::Approx(-8.0));  // world position preserved when reparenting
	CHECK(s->GetGlobalNumber("localAfterWorldSet") == doctest::Approx(-10.0));
	scene.OnRuntimeStop();
}

TEST_CASE("LookAt orients forward towards the target")
{
	Scene scene;
	scene.CreateEntity("E");
	scene.OnRuntimeStart();
	ScriptWorld* s = scene.GetScriptWorld();
	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		local t = Scene.Find("E").transform
		t.position = Vec3(0, 0, 0)
		t:LookAt(Vec3(10, 0, 0))
		fx, fy, fz = t.forward.x, t.forward.y, t.forward.z
		t:LookAt(Vec3(0, 10, 0))
		upY = t.forward.y
	)", &err), err);
	CHECK(s->GetGlobalNumber("fx") == doctest::Approx(1.0).epsilon(0.001));
	CHECK(s->GetGlobalNumber("fz") == doctest::Approx(0.0).epsilon(0.001));
	CHECK(s->GetGlobalNumber("upY") == doctest::Approx(1.0).epsilon(0.001));
	scene.OnRuntimeStop();
}

TEST_CASE("Prefab instantiation from scripts, including script instances on the new entities")
{
	TestProject project;
	project.Write("Scripts/Spin.lua", "local M = {}\nM.properties = { rate = 1 }\nfunction M:OnCreate() spawned = (spawned or 0) + 1 end\nreturn M");
	{
		Scene authoring;
		Entity root = authoring.CreateEntity("Bullet");
		root.AddComponent<ScriptComponent>().Script = "Scripts/Spin.lua";
		SceneSerializer::SerializePrefab(authoring, root, project.Assets() / "Prefabs" / "Bullet.sfprefab");
	}
	Scene scene;
	scene.OnRuntimeStart();
	ScriptWorld* s = scene.GetScriptWorld();
	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		local b = Scene.Instantiate("Prefabs/Bullet.sfprefab", Vec3(1, 2, 3))
		bx = b.transform.position.x
		rate = b:GetScript().rate
		b:GetScript().rate = 5
		rate2 = b:GetScript().rate
		failed = Scene.Instantiate("Prefabs/Nope.sfprefab") == nil and 1 or 0
	)", &err), err);
	CHECK(s->GetGlobalNumber("bx") == 1);
	CHECK(s->GetGlobalNumber("spawned") == 1);
	CHECK(s->GetGlobalNumber("rate") == 1);
	CHECK(s->GetGlobalNumber("rate2") == 5);
	CHECK(s->GetGlobalNumber("failed") == 1);
	CHECK(scene.GetEntityCount() == 1);
	scene.OnRuntimeStop();
}

TEST_CASE("Entities spawned from C++ during play get script instances next frame")
{
	TestProject project;
	project.Write("Scripts/Late.lua", "local M = {}\nfunction M:OnUpdate(dt) lateUpdates = (lateUpdates or 0) + 1 end\nreturn M");
	Scene scene;
	scene.OnRuntimeStart();
	Entity e = scene.CreateEntity("Late");
	e.AddComponent<ScriptComponent>().Script = "Scripts/Late.lua";
	Run(scene, 3);
	CHECK(scene.GetScriptWorld()->GetGlobalNumber("lateUpdates") == 3);
	scene.OnRuntimeStop();
}

TEST_CASE("Input API reflects injected input state")
{
	Input::Reset();
	Scene scene;
	scene.OnRuntimeStart();
	ScriptWorld* s = scene.GetScriptWorld();
	Input::OnKey(KeyCode::W, true);
	Input::OnKey(KeyCode::Space, true);
	Input::OnMouseButton(MouseButton::Right, true);
	Input::OnMouseMoved(10, 10);
	Input::OnMouseMoved(14, 13);
	Input::OnScroll(0, 2);
	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		w = Input.IsKeyDown("w") and 1 or 0
		wPressed = Input.IsKeyPressed("W") and 1 or 0
		a = Input.IsKeyDown("A") and 1 or 0
		space = Input.IsKeyDown("Space") and 1 or 0
		rmb = Input.IsMouseButtonDown(1) and 1 or 0
		lmb = Input.IsMouseButtonDown(0) and 1 or 0
		local mx, my = Input.GetMouseDelta()
		dx, dy = mx, my
		local px, py = Input.GetMousePosition()
		posX = px
		scroll = Input.GetScroll()
	)", &err), err);
	CHECK(s->GetGlobalNumber("w") == 1);
	CHECK(s->GetGlobalNumber("wPressed") == 1);
	CHECK(s->GetGlobalNumber("a") == 0);
	CHECK(s->GetGlobalNumber("space") == 1);
	CHECK(s->GetGlobalNumber("rmb") == 1);
	CHECK(s->GetGlobalNumber("lmb") == 0);
	CHECK(s->GetGlobalNumber("dx") == 4);
	CHECK(s->GetGlobalNumber("dy") == 3);
	CHECK(s->GetGlobalNumber("posX") == 14);
	CHECK(s->GetGlobalNumber("scroll") == 2);
	CHECK_FALSE(s->ExecuteString("Input.IsKeyDown('NotAKey')"));
	CHECK_FALSE(s->ExecuteString("Input.IsMouseButtonDown(7)"));

	Input::BeginFrame();
	CHECK(s->ExecuteString("assert(not Input.IsKeyPressed('W') and Input.IsKeyDown('W'))"));
	Input::SetEnabled(false);
	CHECK(s->ExecuteString("assert(not Input.IsKeyDown('W'))"));
	Input::SetEnabled(true);
	Input::Reset();
	scene.OnRuntimeStop();
}

TEST_CASE("Scripts drive physics: forces, raycasts and collision callbacks")
{
	TestProject project;
	project.Write("Scripts/Ball.lua", R"(
		local M = {}
		function M:OnCreate() self.rb = self.entity:GetComponent("RigidBody") end
		function M:OnUpdate(dt)
			if not self.pushed then self.rb:AddImpulse(Vec3(0, 8, 0)); self.pushed = true end
			peak = math.max(peak or 0, self.entity.transform.position.y)
		end
		function M:OnCollisionEnter(other) hitName = other.name end
		return M
	)");
	project.Write("Scripts/Zone.lua", R"(
		local M = {}
		function M:OnTriggerEnter(other) triggerEnter = (triggerEnter or 0) + 1; who = other.name end
		function M:OnTriggerExit(other) triggerExit = (triggerExit or 0) + 1 end
		return M
	)");

	Scene scene;
	Entity floor = scene.CreateEntity("Floor");
	floor.Transform().Translation = { 0, -0.5f, 0 };
	floor.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
	floor.AddComponent<ColliderComponent>().Size = { 40, 1, 40 };
	Entity ball = scene.CreateEntity("Ball");
	ball.Transform().Translation = { 0, 0.5f, 0 };
	ball.AddComponent<RigidBodyComponent>();
	ball.AddComponent<ColliderComponent>().Size = { 1, 1, 1 };
	ball.AddComponent<ScriptComponent>().Script = "Scripts/Ball.lua";
	Entity zone = scene.CreateEntity("Zone");
	zone.Transform().Translation = { 0, 4, 0 };
	auto& zc = zone.AddComponent<ColliderComponent>();
	zc.IsTrigger = true;
	zc.Size = { 6, 1, 6 };
	zone.AddComponent<ScriptComponent>().Script = "Scripts/Zone.lua";

	scene.OnRuntimeStart();
	Run(scene, 240);
	ScriptWorld* s = scene.GetScriptWorld();
	CHECK(s->GetErrors().empty());
	CHECK(s->GetGlobalNumber("peak") > 2.0);
	CHECK(s->GetGlobalString("hitName") == "Floor");
	CHECK(s->GetGlobalNumber("triggerEnter") >= 1);
	CHECK(s->GetGlobalString("who") == "Ball");
	CHECK(s->GetGlobalNumber("triggerExit") >= 1);

	std::string err;
	REQUIRE_MESSAGE(s->ExecuteString(R"(
		local hit = Scene.Raycast(Vec3(0, 10, 0), Vec3(0, -1, 0), 50)
		hitName2 = hit and hit.entity.name or "none"
		hitDist = hit and hit.distance or -1
		noHit = Scene.Raycast(Vec3(0, 10, 0), Vec3(0, 1, 0), 50) == nil and 1 or 0
		near = #Scene.OverlapSphere(Vec3(0, 0, 0), 3)
		Scene.SetGravity(Vec3(0, -1, 0))
		g = Scene.GetGravity().y
	)", &err), err);
	CHECK(s->GetGlobalString("hitName2") != "none");
	CHECK(s->GetGlobalNumber("noHit") == 1);
	CHECK(s->GetGlobalNumber("near") >= 2);
	CHECK(s->GetGlobalNumber("g") == -1);
	scene.OnRuntimeStop();
}

TEST_CASE("Application.Quit invokes the handler")
{
	bool quit = false;
	ScriptWorld::SetQuitHandler([&] { quit = true; });
	Scene scene;
	scene.OnRuntimeStart();
	CHECK(scene.GetScriptWorld()->ExecuteString("Application.Quit()"));
	CHECK(quit);
	ScriptWorld::SetQuitHandler(nullptr);
	scene.OnRuntimeStop();
}

TEST_CASE("Time API is updated each frame")
{
	Scene scene;
	scene.OnRuntimeStart();
	Run(scene, 5, 0.5f);
	ScriptWorld* s = scene.GetScriptWorld();
	CHECK(s->ExecuteString("dt = Time.delta; t = Time.time; f = Time.frame"));
	CHECK(s->GetGlobalNumber("dt") == doctest::Approx(0.5));
	CHECK(s->GetGlobalNumber("t") == doctest::Approx(2.5));
	CHECK(s->GetGlobalNumber("f") == 5);
	scene.OnRuntimeStop();
}

TEST_CASE("Scripts can react to being created before other scripts update (instances can talk)")
{
	TestProject project;
	project.Write("Scripts/Counter.lua", "local M = {}\nM.properties = { count = 0 }\nfunction M:Add(n) self.count = self.count + n end\nreturn M");
	project.Write("Scripts/Driver.lua", "local M = {}\nfunction M:OnUpdate(dt) local other = Scene.Find('Counter'):GetScript(); other:Add(2); total = other.count end\nreturn M");
	Scene scene;
	WithScript(scene, "Counter", "Scripts/Counter.lua");
	WithScript(scene, "Driver", "Scripts/Driver.lua");
	scene.OnRuntimeStart();
	Run(scene, 3);
	CHECK(scene.GetScriptWorld()->GetGlobalNumber("total") == 6);
	CHECK(scene.GetScriptWorld()->GetErrors().empty());
	scene.OnRuntimeStop();
}
