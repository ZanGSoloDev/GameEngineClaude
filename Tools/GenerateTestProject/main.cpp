// Generates Projects/TestProject: a scene that uses every component, material feature and the whole scripting API.
// Usage: GenerateTestProject <output directory>

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Renderer/Material.h"
#include "Starfall/Renderer/Mesh.h"
#include "Starfall/Renderer/Texture.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <functional>
#include <cstring>
#include <iostream>

using namespace Starfall;
namespace fs = std::filesystem;

namespace {

	fs::path g_Assets;

	void WriteAsset(const std::string& path, const std::string& content)
	{
		if(!FileSystem::WriteText(g_Assets / path, content))
		{
			std::cerr << "failed to write " << path << "\n";
			std::exit(1);
		}
	}

	std::string Base64(const uint8_t* data, size_t size)
	{
		static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		for(size_t i = 0; i < size; i += 3)
		{
			uint32_t n = data[i] << 16;
			if(i + 1 < size) n |= data[i + 1] << 8;
			if(i + 2 < size) n |= data[i + 2];
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += i + 1 < size ? table[(n >> 6) & 63] : '=';
			out += i + 2 < size ? table[n & 63] : '=';
		}
		return out;
	}

	// ---------------- Generated binary assets ----------------

	void WriteCheckerTexture()
	{
		const uint32_t size = 256;
		std::vector<uint8_t> px(size * size * 4);
		for(uint32_t y = 0; y < size; y++)
			for(uint32_t x = 0; x < size; x++)
			{
				bool dark = ((x / 32) + (y / 32)) % 2 == 0;
				uint8_t v = dark ? 60 : 230;
				uint8_t* p = &px[(y * size + x) * 4];
				p[0] = v; p[1] = dark ? 90 : 230; p[2] = dark ? 140 : 230; p[3] = 255;
			}
		ImageIO::SavePNG(g_Assets / "Textures" / "checker.png", size, size, px.data());

		// Alpha-tested leaf-like pattern (circles) for the masked material
		std::vector<uint8_t> mask(size * size * 4);
		for(uint32_t y = 0; y < size; y++)
			for(uint32_t x = 0; x < size; x++)
			{
				float dx = (float(x) - 128.0f) / 128.0f, dy = (float(y) - 128.0f) / 128.0f;
				bool inside = (dx * dx + dy * dy) < 0.6f && ((x / 24 + y / 24) % 2 == 0);
				uint8_t* p = &mask[(y * size + x) * 4];
				p[0] = 60; p[1] = 180; p[2] = 70; p[3] = inside ? 255 : 0;
			}
		ImageIO::SavePNG(g_Assets / "Textures" / "masked.png", size, size, mask.data());

		// Tangent-space bumpy normal map (sine waves)
		std::vector<uint8_t> normal(size * size * 4);
		for(uint32_t y = 0; y < size; y++)
			for(uint32_t x = 0; x < size; x++)
			{
				float fx = std::cos(float(x) * glm::two_pi<float>() / 32.0f) * 0.6f;
				float fy = std::cos(float(y) * glm::two_pi<float>() / 32.0f) * 0.6f;
				glm::vec3 n = glm::normalize(glm::vec3(fx, fy, 1.0f));
				uint8_t* p = &normal[(y * size + x) * 4];
				p[0] = uint8_t((n.x * 0.5f + 0.5f) * 255); p[1] = uint8_t((n.y * 0.5f + 0.5f) * 255); p[2] = uint8_t((n.z * 0.5f + 0.5f) * 255); p[3] = 255;
			}
		ImageIO::SavePNG(g_Assets / "Textures" / "bumps_normal.png", size, size, normal.data());
	}

	void WriteWav(const std::string& path, float seconds, float frequency, int sampleRate = 44100)
	{
		uint32_t samples = uint32_t(seconds * sampleRate);
		uint32_t dataSize = samples * 2;
		std::vector<uint8_t> wav(44 + dataSize);
		auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&wav[at], &v, 4); };
		auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&wav[at], &v, 2); };
		std::memcpy(&wav[0], "RIFF", 4); put32(4, 36 + dataSize); std::memcpy(&wav[8], "WAVEfmt ", 8);
		put32(16, 16); put16(20, 1); put16(22, 1); put32(24, sampleRate); put32(28, sampleRate * 2); put16(32, 2); put16(34, 16);
		std::memcpy(&wav[36], "data", 4); put32(40, dataSize);
		for(uint32_t i = 0; i < samples; i++)
		{
			float t = float(i) / sampleRate;
			float envelope = std::min(1.0f, std::min(t * 50.0f, (seconds - t) * 50.0f));
			int16_t v = int16_t(std::sin(glm::two_pi<float>() * frequency * t) * 9000.0f * envelope);
			std::memcpy(&wav[44 + i * 2], &v, 2);
		}
		FileSystem::WriteBinary(g_Assets / path, wav.data(), wav.size());
	}

	// Radiance RGBE (flat, uncompressed) studio-like HDRI: bright sky gradient with a hot sun spot.
	void WriteHdri()
	{
		const int w = 512, h = 256;
		std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(h) + " +X " + std::to_string(w) + "\n";
		std::vector<uint8_t> bytes(header.begin(), header.end());
		for(int y = 0; y < h; y++)
			for(int x = 0; x < w; x++)
			{
				float v = (float(y) + 0.5f) / h; // 0 top
				float u = (float(x) + 0.5f) / w;
				glm::vec3 sky = glm::mix(glm::vec3(0.25f, 0.45f, 0.9f), glm::vec3(0.9f, 0.85f, 0.8f), std::pow(v, 1.5f));
				if(v > 0.5f)
					sky = glm::mix(glm::vec3(0.9f, 0.85f, 0.8f) * 0.8f, glm::vec3(0.15f, 0.13f, 0.12f), std::min(1.0f, (v - 0.5f) * 3.0f));
				float du = std::min(std::abs(u - 0.62f), 1.0f - std::abs(u - 0.62f));
				float dv = v - 0.3f;
				float d2 = (du * du * 4.0f + dv * dv) * 400.0f;
				sky += glm::vec3(1.0f, 0.9f, 0.7f) * 30.0f * std::exp(-d2);
				float m = std::max(sky.r, std::max(sky.g, sky.b));
				int e;
				float mant = std::frexp(m, &e);
				float scale = mant * 256.0f / m;
				bytes.push_back(uint8_t(sky.r * scale));
				bytes.push_back(uint8_t(sky.g * scale));
				bytes.push_back(uint8_t(sky.b * scale));
				bytes.push_back(uint8_t(e + 128));
			}
		FileSystem::WriteBinary(g_Assets / "HDRI" / "studio.hdr", bytes.data(), bytes.size());
	}

	// glTF with embedded buffer + PNG: a textured cube with normals, uvs and a PBR material.
	void WriteGltfCube()
	{
		auto mesh = MeshFactory::CreateCube();
		const auto& v = mesh->GetVertices();
		const auto& idx = mesh->GetIndices();
		std::vector<uint8_t> buffer;
		auto append = [&](const void* data, size_t size) {
			size_t offset = buffer.size();
			buffer.insert(buffer.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
			return offset;
		};
		std::vector<glm::vec3> positions, normals;
		std::vector<glm::vec2> uvs;
		for(const auto& vert : v) { positions.push_back(vert.Position); normals.push_back(vert.Normal); uvs.push_back(vert.UV); }
		size_t posOff = append(positions.data(), positions.size() * 12);
		size_t norOff = append(normals.data(), normals.size() * 12);
		size_t uvOff = append(uvs.data(), uvs.size() * 8);
		size_t idxOff = append(idx.data(), idx.size() * 4);

		auto png = FileSystem::ReadBinary(g_Assets / "Textures" / "checker.png");
		std::string pngUri = "data:image/png;base64," + Base64(png->data(), png->size());

		std::string gltf = R"({"asset":{"version":"2.0","generator":"Starfall GenerateTestProject"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"ImportedCube","mesh":0,"children":[1]},{"name":"ImportedChild","translation":[0,0.8,0],"scale":[0.3,0.3,0.3],"mesh":0}],
"meshes":[{"name":"Cube","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
"materials":[{"name":"Checker","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0.1,"roughnessFactor":0.5}}],
"textures":[{"source":0}],"images":[{"uri":")" + pngUri + R"("}],
"buffers":[{"byteLength":)" + std::to_string(buffer.size()) + R"(,"uri":"data:application/octet-stream;base64,)" + Base64(buffer.data(), buffer.size()) + R"("}],
"bufferViews":[{"buffer":0,"byteOffset":)" + std::to_string(posOff) + R"(,"byteLength":)" + std::to_string(positions.size() * 12) + R"(},
{"buffer":0,"byteOffset":)" + std::to_string(norOff) + R"(,"byteLength":)" + std::to_string(normals.size() * 12) + R"(},
{"buffer":0,"byteOffset":)" + std::to_string(uvOff) + R"(,"byteLength":)" + std::to_string(uvs.size() * 8) + R"(},
{"buffer":0,"byteOffset":)" + std::to_string(idxOff) + R"(,"byteLength":)" + std::to_string(idx.size() * 4) + R"(}],
"accessors":[{"bufferView":0,"componentType":5126,"count":)" + std::to_string(v.size()) + R"(,"type":"VEC3","min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},
{"bufferView":1,"componentType":5126,"count":)" + std::to_string(v.size()) + R"(,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":)" + std::to_string(v.size()) + R"(,"type":"VEC2"},
{"bufferView":3,"componentType":5125,"count":)" + std::to_string(idx.size()) + R"(,"type":"SCALAR"}]})";
		WriteAsset("Models/Cube.gltf", gltf);
	}

	// ---------------- Scripts ----------------

	void WriteScripts()
	{
		WriteAsset("Scripts/Spinner.lua", R"(-- Rotates the entity around Y. Exported property: speed (degrees per second).
local Spinner = {}
Spinner.properties = { speed = 45.0 }

function Spinner:OnUpdate(dt)
	self.entity.transform:Rotate(Vec3(0, self.speed * dt, 0))
end

return Spinner
)");

		WriteAsset("Scripts/Mover.lua", R"(-- Moves a kinematic body back and forth along an axis.
local Mover = {}
Mover.properties = { amplitude = 3.0, speed = 0.8 }

function Mover:OnCreate()
	self.origin = self.entity.transform.position
end

function Mover:OnUpdate(dt)
	local offset = math.sin(Time.time * self.speed) * self.amplitude
	self.entity.transform.position = self.origin + Vec3(offset, 0, 0)
end

return Mover
)");

		WriteAsset("Scripts/Bullet.lua", R"(-- Self-destroying projectile.
local Bullet = {}
Bullet.properties = { lifetime = 3.0 }

function Bullet:OnCreate()
	self.age = 0
end

function Bullet:OnUpdate(dt)
	self.age = self.age + dt
	if self.age >= self.lifetime then
		self.entity:Destroy()
	end
end

function Bullet:OnCollisionEnter(other)
	BulletHits = (BulletHits or 0) + 1
end

return Bullet
)");

		WriteAsset("Scripts/Player.lua", R"(-- Interactive demo: WASD pushes the ball, Space shoots a bullet prefab, R resets, Escape quits.
local Player = {}
Player.properties = { force = 18.0, shootSpeed = 14.0 }

function Player:OnCreate()
	self.body = self.entity:GetComponent("RigidBody")
	self.cooldown = 0
end

function Player:OnUpdate(dt)
	local push = Vec3(0, 0, 0)
	if Input.IsKeyDown("W") then push = push + Vec3(0, 0, -1) end
	if Input.IsKeyDown("S") then push = push + Vec3(0, 0, 1) end
	if Input.IsKeyDown("A") then push = push + Vec3(-1, 0, 0) end
	if Input.IsKeyDown("D") then push = push + Vec3(1, 0, 0) end
	if push:LengthSquared() > 0 then
		self.body:AddForce(push:Normalized() * self.force)
	end

	self.cooldown = self.cooldown - dt
	if Input.IsKeyPressed("Space") and self.cooldown <= 0 then
		local position = self.entity.transform.worldPosition + Vec3(0, 1.2, 0)
		local bullet = Scene.Instantiate("Prefabs/Bullet.sfprefab", position)
		if bullet then
			bullet:GetComponent("RigidBody").velocity = Vec3(0, 5, -self.shootSpeed)
			Audio.PlayOneShot("Audio/beep.wav", position, 0.4)
		end
		self.cooldown = 0.25
	end

	if Input.IsKeyPressed("R") then
		self.entity.transform.position = Vec3(0, 1, 4)
	end
	if Input.IsKeyPressed("Escape") then
		Application.Quit()
	end

	local camera = Scene.GetMainCamera()
	if camera then
		local target = self.entity.transform.worldPosition
		camera.transform.position = target + Vec3(0, 5, 9)
		camera.transform:LookAt(target)
	end
end

return Player
)");

		WriteAsset("Scripts/Ball.lua", R"(local Ball = {}
function Ball:OnCollisionEnter(other)
	BallHits = (BallHits or 0) + 1
	BallHitName = other.name
end
return Ball
)");

		WriteAsset("Scripts/Zone.lua", R"(local Zone = {}
function Zone:OnTriggerEnter(other) TriggerEnters = (TriggerEnters or 0) + 1 end
function Zone:OnTriggerExit(other) TriggerExits = (TriggerExits or 0) + 1 end
return Zone
)");

		// Exercises the entire scripting API and records the outcome in globals read by the automated tests.
		WriteAsset("Scripts/SelfTest.lua", R"(-- Automated API self test. Results are published as globals: SelfTestDone, SelfTestFailures, SelfTestChecks.
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
)");
	}

	// ---------------- Materials ----------------

	void SaveMaterial(const std::string& name, const std::function<void(Material&)>& setup)
	{
		Material m(name);
		setup(m);
		MaterialSerializer::Save(m, g_Assets / "Materials" / (name + ".sfmat"));
	}

	void WriteMaterials()
	{
		SaveMaterial("Gold", [](Material& m) { m.GetData().BaseColor = { 1.0f, 0.77f, 0.34f, 1 }; m.GetData().Metallic = 1.0f; m.GetData().Roughness = 0.25f; });
		SaveMaterial("RedPlastic", [](Material& m) { m.GetData().BaseColor = { 0.8f, 0.05f, 0.05f, 1 }; m.GetData().Metallic = 0.0f; m.GetData().Roughness = 0.35f; });
		SaveMaterial("RoughStone", [](Material& m) { m.GetData().BaseColor = { 0.45f, 0.45f, 0.48f, 1 }; m.GetData().Roughness = 0.95f; });
		SaveMaterial("Mirror", [](Material& m) { m.GetData().BaseColor = { 0.95f, 0.95f, 0.95f, 1 }; m.GetData().Metallic = 1.0f; m.GetData().Roughness = 0.02f; });
		SaveMaterial("Emissive", [](Material& m) { m.GetData().BaseColor = { 0.05f, 0.05f, 0.05f, 1 }; m.GetData().Emissive = { 0.2f, 0.9f, 1.0f }; m.GetData().EmissiveIntensity = 6.0f; });
		SaveMaterial("Glass", [](Material& m) { m.GetData().BaseColor = { 0.4f, 0.7f, 1.0f, 0.35f }; m.GetData().Roughness = 0.05f; m.GetData().Alpha = AlphaMode::Blend; });
		SaveMaterial("Checker", [](Material& m) { m.SetTexturePath(TextureSlot::BaseColor, "Textures/checker.png"); m.GetData().Roughness = 0.6f; m.GetData().UVScale = { 6, 6 }; });
		SaveMaterial("Bumpy", [](Material& m) {
			m.GetData().BaseColor = { 0.7f, 0.5f, 0.35f, 1 }; m.GetData().Roughness = 0.5f; m.GetData().NormalScale = 1.0f;
			m.SetTexturePath(TextureSlot::Normal, "Textures/bumps_normal.png"); m.GetData().UVScale = { 2, 2 }; });
		SaveMaterial("MaskedLeaves", [](Material& m) { m.SetTexturePath(TextureSlot::BaseColor, "Textures/masked.png"); m.GetData().Alpha = AlphaMode::Mask; m.GetData().DoubleSided = true; });
		for(int i = 0; i < 5; i++)
			SaveMaterial("Roughness" + std::to_string(i), [i](Material& m) { m.GetData().BaseColor = { 0.9f, 0.2f, 0.2f, 1 }; m.GetData().Metallic = 1.0f; m.GetData().Roughness = 0.1f + 0.2f * i; });
	}

	// ---------------- Scene ----------------

	Entity AddMesh(Scene& scene, const std::string& name, const std::string& mesh, glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f), const std::string& material = "")
	{
		Entity e = scene.CreateEntity(name);
		e.Transform().Translation = position;
		e.Transform().Scale = scale;
		auto& mr = e.AddComponent<MeshRendererComponent>();
		mr.Mesh = mesh;
		mr.Material = material.empty() ? "" : "Materials/" + material + ".sfmat";
		return e;
	}

	void AddPhysics(Entity e, BodyType type, ColliderShape shape, glm::vec3 size, float mass = 1.0f)
	{
		auto& rb = e.AddComponent<RigidBodyComponent>();
		rb.Type = type;
		rb.Mass = mass;
		auto& c = e.AddComponent<ColliderComponent>();
		c.Shape = shape;
		c.Size = size;
	}

	void BuildScene(Scene& scene)
	{
		scene.SetName("Main");
		auto& s = scene.GetSettings();
		s.Environment.HDRI = "HDRI/studio.hdr";
		s.Environment.Intensity = 0.9f;
		s.Environment.RotationDegrees = 20.0f;
		s.PostProcess.Exposure = 0.8f;
		s.PostProcess.TonemapMode = Tonemapper::ACES;

		// Ground
		Entity floor = AddMesh(scene, "Floor", "builtin://Cube", { 0, -0.5f, 0 }, { 40, 1, 40 }, "Checker");
		AddPhysics(floor, BodyType::Static, ColliderShape::Box, { 1, 1, 1 });
		Entity wall = AddMesh(scene, "Wall", "builtin://Cube", { 0, 2, -9 }, { 14, 4, 0.5f }, "RoughStone");
		AddPhysics(wall, BodyType::Static, ColliderShape::Box, { 1, 1, 1 });

		// Lighting
		Entity sun = scene.CreateEntity("Sun");
		sun.Transform().Rotation = { glm::radians(-48.0f), glm::radians(35.0f), 0 };
		auto& sl = sun.AddComponent<LightComponent>();
		sl.Type = LightType::Directional; sl.Intensity = 3.0f; sl.Color = { 1.0f, 0.95f, 0.85f }; sl.ShadowSoftness = 1.0f;

		Entity point = scene.CreateEntity("PointLight");
		point.Transform().Translation = { -4, 2.5f, 1 };
		auto& pl = point.AddComponent<LightComponent>();
		pl.Type = LightType::Point; pl.Color = { 1.0f, 0.5f, 0.3f }; pl.Intensity = 40.0f; pl.Range = 10.0f;

		Entity spot = scene.CreateEntity("SpotLight");
		spot.Transform().Translation = { 5, 4, 2 };
		spot.Transform().Rotation = { glm::radians(-65.0f), glm::radians(20.0f), 0 };
		auto& spl = spot.AddComponent<LightComponent>();
		spl.Type = LightType::Spot; spl.Color = { 0.4f, 0.6f, 1.0f }; spl.Intensity = 120.0f; spl.Range = 14.0f;
		spl.InnerConeAngle = glm::radians(18.0f); spl.OuterConeAngle = glm::radians(32.0f);

		// Camera, listener, ambient sound
		Entity camera = scene.CreateEntity("MainCamera");
		camera.Transform().Translation = { 0, 5, 12 };
		camera.Transform().Rotation = { glm::radians(-18.0f), 0, 0 };
		auto& cc = camera.AddComponent<CameraComponent>();
		cc.VerticalFOV = glm::radians(50.0f);
		cc.Primary = true;
		camera.AddComponent<AudioListenerComponent>();
		scene.CreateEntity("Listener").AddComponent<AudioListenerComponent>().Active = true;

		Entity ambient = scene.CreateEntity("AmbientSound");
		ambient.Transform().Translation = { 0, 1, 0 };
		auto& as = ambient.AddComponent<AudioSourceComponent>();
		as.Clip = "Audio/hum.wav"; as.Loop = true; as.PlayOnStart = true; as.Volume = 0.15f; as.MinDistance = 2.0f; as.MaxDistance = 30.0f;

		// PBR showcase: metal roughness sweep + dielectric materials
		for(int i = 0; i < 5; i++)
			AddMesh(scene, "Sphere" + std::to_string(i), "builtin://Sphere", { -4.0f + i * 2.0f, 1.0f, -5.0f }, glm::vec3(1.6f), "Roughness" + std::to_string(i));
		AddMesh(scene, "GoldSphere", "builtin://Sphere", { -6, 1, -2 }, glm::vec3(1.5f), "Gold");
		AddMesh(scene, "MirrorSphere", "builtin://Sphere", { 6, 1, -2 }, glm::vec3(1.5f), "Mirror");
		AddMesh(scene, "PlasticCapsule", "builtin://Capsule", { -7, 1, 2 }, glm::vec3(1.0f), "RedPlastic");
		AddMesh(scene, "BumpyCylinder", "builtin://Cylinder", { 7.5f, 1, 2 }, { 1.4f, 2, 1.4f }, "Bumpy");
		AddMesh(scene, "Cone", "builtin://Cone", { 9, 1, -3 }, { 1.5f, 2, 1.5f }, "RoughStone");
		AddMesh(scene, "EmissiveCube", "builtin://Cube", { 2, 0.5f, -3 }, glm::vec3(1.0f), "Emissive");
		AddMesh(scene, "GlassSphere", "builtin://Sphere", { 3.5f, 1.0f, 0 }, glm::vec3(1.8f), "Glass");
		AddMesh(scene, "LeafPlane", "builtin://Quad", { -2, 1.3f, -1 }, glm::vec3(2.5f), "MaskedLeaves");
		Entity spinning = AddMesh(scene, "SpinningCube", "builtin://Cube", { 0, 2.2f, -2 }, glm::vec3(1.2f), "Gold");
		spinning.AddComponent<ScriptComponent>().Script = "Scripts/Spinner.lua";
		spinning.GetComponent<ScriptComponent>().Properties["speed"] = 90.0;
		spinning.GetComponent<MeshRendererComponent>().CastShadows = true;

		// Imported glTF model (hierarchy + embedded material)
		Entity model = scene.CreateEntity("ImportedModel");
		model.Transform().Translation = { 9, 0.5f, 3 };
		model.AddComponent<MeshRendererComponent>().Mesh = "Models/Cube.gltf#0";

		// Physics: crate stack, falling ball (collision callback), trigger zone, kinematic platform
		for(int i = 0; i < 4; i++)
		{
			Entity crate = AddMesh(scene, "Crate" + std::to_string(i + 1), "builtin://Cube", { -9.0f, 0.5f + i * 1.05f, 4.0f }, glm::vec3(1.0f), "Bumpy");
			AddPhysics(crate, BodyType::Dynamic, ColliderShape::Box, { 1, 1, 1 }, 2.0f);
		}
		Entity ball = AddMesh(scene, "FallingBall", "builtin://Sphere", { 8, 7, 8 }, glm::vec3(1.0f), "RedPlastic");
		AddPhysics(ball, BodyType::Dynamic, ColliderShape::Sphere, { 1, 1, 1 }, 1.0f);
		ball.GetComponent<ColliderComponent>().Restitution = 0.5f;
		ball.AddComponent<ScriptComponent>().Script = "Scripts/Ball.lua";

		Entity zone = scene.CreateEntity("TriggerZone");
		zone.Transform().Translation = { 8, 3.5f, 8 };
		auto& zc = zone.AddComponent<ColliderComponent>();
		zc.IsTrigger = true; zc.Size = { 3, 2, 3 };
		zone.AddComponent<ScriptComponent>().Script = "Scripts/Zone.lua";

		Entity platform = AddMesh(scene, "MovingPlatform", "builtin://Cube", { 0, 0.3f, 6 }, { 3, 0.4f, 2 }, "RoughStone");
		AddPhysics(platform, BodyType::Kinematic, ColliderShape::Box, { 1, 1, 1 });
		platform.AddComponent<ScriptComponent>().Script = "Scripts/Mover.lua";

		Entity player = AddMesh(scene, "Player", "builtin://Sphere", { 0, 1, 4 }, glm::vec3(1.0f), "Gold");
		AddPhysics(player, BodyType::Dynamic, ColliderShape::Sphere, { 1, 1, 1 }, 2.0f);
		player.GetComponent<RigidBodyComponent>().AngularDamping = 0.5f;
		player.GetComponent<RigidBodyComponent>().Continuous = true;
		player.GetComponent<ColliderComponent>().Friction = 0.8f;
		player.AddComponent<ScriptComponent>().Script = "Scripts/Player.lua";

		Entity capsule = AddMesh(scene, "CapsuleBody", "builtin://Capsule", { 6, 3, 5 }, glm::vec3(1.0f), "Gold");
		AddPhysics(capsule, BodyType::Dynamic, ColliderShape::Capsule, { 1, 2, 1 }, 1.5f);
		capsule.GetComponent<RigidBodyComponent>().LockRotationX = true;

		// Hierarchy example
		Entity parent = scene.CreateEntity("RotatingParent");
		parent.Transform().Translation = { -9, 3, -4 };
		parent.AddComponent<ScriptComponent>().Script = "Scripts/Spinner.lua";
		Entity childA = AddMesh(scene, "ChildA", "builtin://Cube", { 1.5f, 0, 0 }, glm::vec3(0.6f), "Gold");
		Entity childB = AddMesh(scene, "ChildB", "builtin://Sphere", { -1.5f, 0, 0 }, glm::vec3(0.6f), "RedPlastic");
		scene.SetParent(childA, parent, false);
		scene.SetParent(childB, parent, false);

		// Test runner
		scene.CreateEntity("SelfTest").AddComponent<ScriptComponent>().Script = "Scripts/SelfTest.lua";
	}

	void BuildPrefab()
	{
		Scene authoring;
		Entity bullet = authoring.CreateEntity("Bullet");
		bullet.Transform().Scale = glm::vec3(0.35f);
		bullet.AddComponent<MeshRendererComponent>().Mesh = "builtin://Sphere";
		bullet.GetComponent<MeshRendererComponent>().Material = "Materials/Emissive.sfmat";
		AddPhysics(bullet, BodyType::Dynamic, ColliderShape::Sphere, { 1, 1, 1 }, 0.3f);
		bullet.AddComponent<ScriptComponent>().Script = "Scripts/Bullet.lua";
		Entity glow = authoring.CreateEntity("Glow");
		auto& l = glow.AddComponent<LightComponent>();
		l.Type = LightType::Point; l.Color = { 0.2f, 0.9f, 1.0f }; l.Intensity = 6.0f; l.Range = 4.0f; l.CastShadows = false;
		authoring.SetParent(glow, bullet, false);
		SceneSerializer::SerializePrefab(authoring, bullet, g_Assets / "Prefabs" / "Bullet.sfprefab");
	}

}

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::cerr << "usage: GenerateTestProject <output directory>\n";
		return 2;
	}
	Log::Init();
	Log::SetLevel(LogLevel::Warn);
	UUID::Seed(20260101); // deterministic output so regenerating does not churn version control

	fs::path root = argv[1];
	fs::create_directories(root);
	if(!Project::Create(root, "TestProject"))
	{
		std::cerr << "could not create project\n";
		return 1;
	}
	Project::SetStartScene("Scenes/Main.sfscene");
	Project::Save();
	g_Assets = Project::GetAssetDirectory();

	WriteCheckerTexture();
	WriteWav("Audio/beep.wav", 0.2f, 880.0f);
	WriteWav("Audio/hum.wav", 2.0f, 110.0f);
	WriteHdri();
	WriteGltfCube();
	WriteScripts();
	WriteMaterials();
	BuildPrefab();

	Scene scene;
	BuildScene(scene);
	SceneSerializer::Serialize(scene, g_Assets / "Scenes" / "Main.sfscene");
	std::cout << "Generated test project at " << root.string() << "\n";
	return 0;
}
