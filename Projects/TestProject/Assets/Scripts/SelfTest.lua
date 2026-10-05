-- Automated API self test. Results are published as globals: SelfTestDone, SelfTestFailures, SelfTestChecks.
local SelfTest = {}

local failures, checks = 0, 0
local function check(condition, message)
	checks = checks + 1
	if not condition then
		failures = failures + 1
		Log.Error("SelfTest FAILED: " .. message)
	end
end
local function near(a, b, epsilon) return math.abs(a - b) <= (epsilon or 1e-3) end

function SelfTest:OnCreate()
	self.frame = 0

	-- Vec3 / Mathf
	local a, b = Vec3(1, 2, 3), Vec3(4, 5, 6)
	check((a + b).x == 5 and (b - a).z == 3, "vec3 add/sub")
	check((a * 2).y == 4 and (2 * a).z == 6 and (b / 2).x == 2, "vec3 scale/divide")
	check(a:Dot(b) == 32, "vec3 dot")
	check(Vec3(1, 0, 0):Cross(Vec3(0, 1, 0)).z == 1, "vec3 cross")
	check(near(Vec3(3, 4, 0):Length(), 5), "vec3 length")
	check(near(Vec3(0, 0, 9):Normalized().z, 1), "vec3 normalized")
	check(near(Vec3(0, 0, 0):Distance(Vec3(0, 3, 4)), 5), "vec3 distance")
	check(near(Vec3(0, 0, 0):Lerp(Vec3(10, 0, 0), 0.25).x, 2.5), "vec3 lerp")
	check(a == Vec3(1, 2, 3) and tostring(a):find("Vec3") == 1, "vec3 equality/tostring")
	check(near(Mathf.Lerp(0, 10, 0.5), 5) and Mathf.Clamp(5, 0, 1) == 1 and Mathf.Sign(-2) == -1, "mathf")
	check(near(Mathf.Degrees(Mathf.pi), 180) and near(Mathf.Radians(180), Mathf.pi), "mathf angles")
	check(Mathf.MoveTowards(0, 10, 3) == 3, "mathf move towards")

	-- Scene lookup and entity API
	local floor = Scene.Find("Floor")
	check(floor ~= nil and floor.valid and floor.name == "Floor", "find entity")
	check(Scene.Find("DoesNotExist") == nil, "find missing entity")
	check(Scene.FindByID(floor.id).name == "Floor", "find by id")
	check(Scene.GetMainCamera() ~= nil, "main camera")
	check(#Scene.GetEntities() > 20, "entity list")
	check(floor:HasComponent("Collider") and floor:HasComponent("RigidBody") and floor:HasComponent("MeshRenderer"), "has component")
	check(floor:GetComponent("Light") == nil, "missing component is nil")
	check(floor:GetComponent("Collider").shape == "Box" and floor:GetComponent("RigidBody").type == "Static", "component readback")

	-- Components
	local sun = Scene.Find("Sun")
	local light = sun:GetComponent("Light")
	check(light.type == "Directional" and light.intensity > 0 and light.castShadows, "light component")
	light.intensity = light.intensity   -- write path
	local cam = Scene.GetMainCamera():GetComponent("Camera")
	check(near(cam.fov, 50, 0.01) and cam.primary and not cam.orthographic, "camera component")
	local mesh = Scene.Find("Crate1"):GetComponent("MeshRenderer")
	check(mesh.mesh == "builtin://Cube" and mesh.castShadows, "mesh renderer")
	local audio = Scene.Find("AmbientSound"):GetComponent("AudioSource")
	check(audio.clip == "Audio/hum.wav" and audio.loop, "audio source")
	check(Scene.Find("Listener"):GetComponent("AudioListener").active, "audio listener")

	-- Transform: set/get, translate/rotate, hierarchy, look at
	local temp = Scene.Create("TempEntity")
	temp.transform.position = Vec3(0, 20, 0)
	temp.transform.scale = Vec3(2, 2, 2)
	temp.transform.rotation = Vec3(0, 90, 0)
	check(temp.transform.position.y == 20 and temp.transform.scale.x == 2, "transform set/get")
	check(near(temp.transform.right.z, -1, 0.01) or near(temp.transform.right.z, 1, 0.01), "transform axes")
	temp.transform:Translate(Vec3(1, 0, 0))
	check(temp.transform.position.x == 1, "translate")
	local parent = Scene.Create("TempParent")
	parent.transform.position = Vec3(10, 0, 0)
	temp.parent = parent
	check(near(temp.transform.worldPosition.x, 1) and near(temp.transform.position.x, -9), "reparent keeps world transform")
	check(#parent:GetChildren() == 1 and parent:FindChild("TempEntity") ~= nil, "children")
	temp.transform:LookAt(Vec3(100, 20, 0))
	check(near(temp.transform.forward.x, 1, 0.01), "look at")
	temp:AddComponent("Light").type = "Spot"
	check(temp:HasComponent("Light") and temp:GetComponent("Light").type == "Spot", "add component")
	temp:RemoveComponent("Light")
	check(not temp:HasComponent("Light"), "remove component")
	self.temp, self.tempParent = temp, parent
	temp:Destroy()
	parent:Destroy()

	-- Script instances talking to each other
	local spinnerEntity = Scene.Find("SpinningCube")
	local spinner = spinnerEntity:GetScript()
	check(spinner ~= nil and spinner.speed == 90, "script property override")

	-- Prefab instantiation and runtime script creation
	self.bullet = Scene.Instantiate("Prefabs/Bullet.sfprefab", Vec3(0, 8, 0))
	check(self.bullet ~= nil and self.bullet.valid, "instantiate prefab")
	check(near(self.bullet.transform.position.y, 8), "instantiate position")
	check(self.bullet:GetScript().lifetime == 3, "prefab script instance")
	check(Scene.Instantiate("Prefabs/Missing.sfprefab") == nil, "missing prefab returns nil")

	-- Physics API
	local ball = Scene.Find("FallingBall")
	local body = ball:GetComponent("RigidBody")
	body.velocity = body.velocity
	check(body.type == "Dynamic" and body.mass > 0, "rigid body")
	body:AddImpulse(Vec3(0, 0, 0))
	body:AddForce(Vec3(0, 0, 0))
	body:AddTorque(Vec3(0, 0, 0))
	Scene.SetGravity(Vec3(0, -9.81, 0))
	check(near(Scene.GetGravity().y, -9.81), "gravity")

	-- Audio / log / time
	Audio.PlayOneShot("Audio/beep.wav", Vec3(0, 1, 0), 0.05)
	Audio.SetMasterVolume(Audio.GetMasterVolume())
	Log.Info("SelfTest started; frame time", Time.delta)
end

function SelfTest:OnUpdate(dt)
	self.frame = self.frame + 1
	check(Time.delta > 0 and Time.time > 0, "time values")

	if self.frame == 3 then
		check(not self.temp.valid and not self.tempParent.valid, "queued destroy completed")
	end

	if self.frame == 120 then
		local hit = Scene.Raycast(Vec3(14, 30, -14), Vec3(0, -1, 0), 100)
		check(hit ~= nil and hit.distance > 0 and hit.point.y < 1, "raycast hits the floor")
		check(Scene.Raycast(Vec3(14, 30, -14), Vec3(0, 1, 0), 100) == nil, "raycast away misses")
		check(#Scene.OverlapSphere(Vec3(0, 0.5, 0), 30) > 3, "overlap sphere")
	end

	if self.frame == 240 then
		check((BallHits or 0) >= 1 and BallHitName ~= nil, "ball collision callback")
		check((TriggerEnters or 0) >= 1, "trigger enter callback")
		check(not self.bullet.valid or self.bullet.transform.position.y < 7, "bullet fell")
		SelfTestFailures = failures
		SelfTestChecks = checks
		SelfTestDone = 1
		Log.Info(string.format("SelfTest finished: %d checks, %d failures", checks, failures))
	end
end

return SelfTest
