#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Math/MathUtils.h"
#include "Starfall/Physics/PhysicsWorld.h"
#include "Starfall/Platform/Input.h"
#include "Starfall/Scripting/ScriptInternal.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace Starfall {

	namespace {

		// Proxy for a component on an entity; re-resolved on each access so a destroyed entity raises a Lua error instead of crashing.
		template<typename T>
		struct ComponentRef
		{
			Scene* SceneRef = nullptr;
			uint64_t ID = 0;

			T& Get() const
			{
				Entity entity = SceneRef ? SceneRef->FindEntityByUUID(UUID(ID)) : Entity();
				T* component = entity ? entity.TryGetComponent<T>() : nullptr;
				if(!component)
					throw std::runtime_error("component no longer exists (entity destroyed or component removed)");
				return *component;
			}

			Entity GetEntity() const { return SceneRef->FindEntityByUUID(UUID(ID)); }
		};

		using TransformRef = ComponentRef<TransformComponent>;

		Entity Resolve(const ScriptEntity& e)
		{
			Entity entity = e.Resolve();
			if(!entity)
				throw std::runtime_error("entity no longer exists");
			return entity;
		}

		ScriptEntity ToScript(Scene* scene, Entity entity) { return { scene, static_cast<uint64_t>(entity.GetUUID()) }; }

		void NotifyTransformChanged(Scene* scene, Entity entity)
		{
			if(PhysicsWorld* physics = scene->GetPhysicsWorld())
				physics->Teleport(entity);
		}

		glm::vec3 WorldPosition(Scene* scene, Entity entity) { return glm::vec3(scene->GetWorldTransform(entity)[3]); }

		glm::vec3 Axis(Scene* scene, Entity entity, int column)
		{
			glm::vec3 v = glm::vec3(scene->GetWorldTransform(entity)[column]);
			float length = glm::length(v);
			return length > 1e-8f ? v / length : v;
		}

		template<typename T>
		sol::object MakeRef(sol::state_view lua, Scene* scene, Entity entity)
		{
			return sol::make_object(lua, ComponentRef<T>{ scene, static_cast<uint64_t>(entity.GetUUID()) });
		}

		template<typename T>
		bool AddIfMissing(Entity entity)
		{
			if(entity.HasComponent<T>())
				return false;
			entity.AddComponent<T>();
			return true;
		}

		const char* LightTypeName(LightType t) { return t == LightType::Directional ? "Directional" : t == LightType::Spot ? "Spot" : "Point"; }
		LightType LightTypeFromName(const std::string& s)
		{
			if(s == "Directional") return LightType::Directional;
			if(s == "Spot") return LightType::Spot;
			if(s == "Point") return LightType::Point;
			throw std::runtime_error("unknown light type '" + s + "' (use Directional, Point or Spot)");
		}

		const char* BodyTypeName(BodyType t) { return t == BodyType::Static ? "Static" : t == BodyType::Kinematic ? "Kinematic" : "Dynamic"; }
		const char* ShapeName(ColliderShape s) { return s == ColliderShape::Box ? "Box" : s == ColliderShape::Sphere ? "Sphere" : "Capsule"; }

		void RegisterMath(sol::state& lua)
		{
			lua.new_usertype<glm::vec3>("Vec3",
				sol::call_constructor, sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
				"x", sol::property([](const glm::vec3& v) { return v.x; }, [](glm::vec3& v, float x) { v.x = x; }),
				"y", sol::property([](const glm::vec3& v) { return v.y; }, [](glm::vec3& v, float y) { v.y = y; }),
				"z", sol::property([](const glm::vec3& v) { return v.z; }, [](glm::vec3& v, float z) { v.z = z; }),
				"Length", [](const glm::vec3& v) { return glm::length(v); },
				"LengthSquared", [](const glm::vec3& v) { return glm::dot(v, v); },
				"Normalized", [](const glm::vec3& v) { float l = glm::length(v); return l > 1e-8f ? v / l : glm::vec3(0.0f); },
				"Dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
				"Cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); },
				"Distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); },
				"Lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); },
				sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
				sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
				sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
				sol::meta_function::multiplication, sol::overload(
					[](const glm::vec3& a, float s) { return a * s; },
					[](float s, const glm::vec3& a) { return a * s; },
					[](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
				sol::meta_function::division, sol::overload(
					[](const glm::vec3& a, float s) { return a / s; },
					[](const glm::vec3& a, const glm::vec3& b) { return a / b; }),
				sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
				sol::meta_function::to_string, [](const glm::vec3& v) { return std::format("Vec3({}, {}, {})", v.x, v.y, v.z); });

			sol::table mathf = lua.create_named_table("Mathf");
			mathf["pi"] = Math::Pi;
			mathf["Lerp"] = [](float a, float b, float t) { return a + (b - a) * t; };
			mathf["Clamp"] = [](float v, float lo, float hi) { return std::min(std::max(v, lo), hi); };
			mathf["Radians"] = [](float d) { return glm::radians(d); };
			mathf["Degrees"] = [](float r) { return glm::degrees(r); };
			mathf["Sign"] = [](float v) { return v > 0.0f ? 1.0f : v < 0.0f ? -1.0f : 0.0f; };
			mathf["MoveTowards"] = [](float current, float target, float maxDelta) {
				return std::abs(target - current) <= maxDelta ? target : current + std::copysign(maxDelta, target - current);
			};
		}

		void RegisterComponents(sol::state& lua)
		{
			lua.new_usertype<TransformRef>("Transform", sol::no_constructor,
				"position", sol::property(
					[](TransformRef& r) { return r.Get().Translation; },
					[](TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; NotifyTransformChanged(r.SceneRef, r.GetEntity()); }),
				"rotation", sol::property( // Euler angles in degrees
					[](TransformRef& r) { return glm::degrees(r.Get().Rotation); },
					[](TransformRef& r, const glm::vec3& v) { r.Get().Rotation = glm::radians(v); NotifyTransformChanged(r.SceneRef, r.GetEntity()); }),
				"scale", sol::property(
					[](TransformRef& r) { return r.Get().Scale; },
					[](TransformRef& r, const glm::vec3& v) { r.Get().Scale = v; }),
				"worldPosition", sol::property(
					[](TransformRef& r) { return WorldPosition(r.SceneRef, r.GetEntity()); },
					[](TransformRef& r, const glm::vec3& v) {
						Entity e = r.GetEntity();
						glm::mat4 world = r.SceneRef->GetWorldTransform(e);
						world[3] = glm::vec4(v, 1.0f);
						r.SceneRef->SetWorldTransform(e, world);
						NotifyTransformChanged(r.SceneRef, e);
					}),
				"forward", sol::property([](TransformRef& r) { return -Axis(r.SceneRef, r.GetEntity(), 2); }),
				"right", sol::property([](TransformRef& r) { return Axis(r.SceneRef, r.GetEntity(), 0); }),
				"up", sol::property([](TransformRef& r) { return Axis(r.SceneRef, r.GetEntity(), 1); }),
				"Translate", [](TransformRef& r, const glm::vec3& delta) {
					r.Get().Translation += delta;
					NotifyTransformChanged(r.SceneRef, r.GetEntity());
				},
				"Rotate", [](TransformRef& r, const glm::vec3& degrees) {
					r.Get().Rotation += glm::radians(degrees);
					NotifyTransformChanged(r.SceneRef, r.GetEntity());
				},
				"LookAt", [](TransformRef& r, const glm::vec3& target) {
					Entity e = r.GetEntity();
					glm::mat4 world = r.SceneRef->GetWorldTransform(e);
					glm::vec3 position(world[3]);
					glm::vec3 direction = target - position;
					if(glm::length(direction) < 1e-6f)
						return;
					direction = glm::normalize(direction);
					glm::vec3 up = std::abs(direction.y) > 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
					glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
					glm::mat4 result = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(glm::quatLookAt(direction, up)) * glm::scale(glm::mat4(1.0f), scale);
					r.SceneRef->SetWorldTransform(e, result);
					NotifyTransformChanged(r.SceneRef, e);
				});

			using Cam = ComponentRef<CameraComponent>;
			lua.new_usertype<Cam>("Camera", sol::no_constructor,
				"fov", sol::property([](Cam& c) { return glm::degrees(c.Get().VerticalFOV); }, [](Cam& c, float v) { c.Get().VerticalFOV = glm::radians(v); }),
				"near", sol::property([](Cam& c) { return c.Get().PerspectiveNear; }, [](Cam& c, float v) { c.Get().PerspectiveNear = v; }),
				"far", sol::property([](Cam& c) { return c.Get().PerspectiveFar; }, [](Cam& c, float v) { c.Get().PerspectiveFar = v; }),
				"orthographic", sol::property([](Cam& c) { return c.Get().Projection == ProjectionType::Orthographic; },
					[](Cam& c, bool v) { c.Get().Projection = v ? ProjectionType::Orthographic : ProjectionType::Perspective; }),
				"orthographicSize", sol::property([](Cam& c) { return c.Get().OrthographicSize; }, [](Cam& c, float v) { c.Get().OrthographicSize = v; }),
				"primary", sol::property([](Cam& c) { return c.Get().Primary; }, [](Cam& c, bool v) { c.Get().Primary = v; }));

			using MR = ComponentRef<MeshRendererComponent>;
			lua.new_usertype<MR>("MeshRenderer", sol::no_constructor,
				"mesh", sol::property([](MR& c) { return c.Get().Mesh; }, [](MR& c, const std::string& v) { c.Get().Mesh = v; }),
				"material", sol::property([](MR& c) { return c.Get().Material; }, [](MR& c, const std::string& v) { c.Get().Material = v; }),
				"castShadows", sol::property([](MR& c) { return c.Get().CastShadows; }, [](MR& c, bool v) { c.Get().CastShadows = v; }),
				"receiveShadows", sol::property([](MR& c) { return c.Get().ReceiveShadows; }, [](MR& c, bool v) { c.Get().ReceiveShadows = v; }),
				"visible", sol::property([](MR& c) { return c.Get().Visible; }, [](MR& c, bool v) { c.Get().Visible = v; }));

			using Lt = ComponentRef<LightComponent>;
			lua.new_usertype<Lt>("Light", sol::no_constructor,
				"type", sol::property([](Lt& c) { return std::string(LightTypeName(c.Get().Type)); }, [](Lt& c, const std::string& v) { c.Get().Type = LightTypeFromName(v); }),
				"color", sol::property([](Lt& c) { return c.Get().Color; }, [](Lt& c, const glm::vec3& v) { c.Get().Color = v; }),
				"intensity", sol::property([](Lt& c) { return c.Get().Intensity; }, [](Lt& c, float v) { c.Get().Intensity = v; }),
				"range", sol::property([](Lt& c) { return c.Get().Range; }, [](Lt& c, float v) { c.Get().Range = v; }),
				"innerCone", sol::property([](Lt& c) { return glm::degrees(c.Get().InnerConeAngle); }, [](Lt& c, float v) { c.Get().InnerConeAngle = glm::radians(v); }),
				"outerCone", sol::property([](Lt& c) { return glm::degrees(c.Get().OuterConeAngle); }, [](Lt& c, float v) { c.Get().OuterConeAngle = glm::radians(v); }),
				"castShadows", sol::property([](Lt& c) { return c.Get().CastShadows; }, [](Lt& c, bool v) { c.Get().CastShadows = v; }),
				"shadowSoftness", sol::property([](Lt& c) { return c.Get().ShadowSoftness; }, [](Lt& c, float v) { c.Get().ShadowSoftness = v; }));

			using RB = ComponentRef<RigidBodyComponent>;
			lua.new_usertype<RB>("RigidBody", sol::no_constructor,
				"type", sol::property([](RB& c) { return std::string(BodyTypeName(c.Get().Type)); }),
				"mass", sol::property([](RB& c) { return c.Get().Mass; }),
				"gravityFactor", sol::property([](RB& c) { return c.Get().GravityFactor; }, [](RB& c, float v) { c.Get().GravityFactor = v; }),
				"velocity", sol::property(
					[](RB& c) { PhysicsWorld* p = c.SceneRef->GetPhysicsWorld(); return p ? p->GetLinearVelocity(c.GetEntity()) : glm::vec3(0.0f); },
					[](RB& c, const glm::vec3& v) { if(PhysicsWorld* p = c.SceneRef->GetPhysicsWorld()) p->SetLinearVelocity(c.GetEntity(), v); }),
				"angularVelocity", sol::property(
					[](RB& c) { PhysicsWorld* p = c.SceneRef->GetPhysicsWorld(); return p ? p->GetAngularVelocity(c.GetEntity()) : glm::vec3(0.0f); },
					[](RB& c, const glm::vec3& v) { if(PhysicsWorld* p = c.SceneRef->GetPhysicsWorld()) p->SetAngularVelocity(c.GetEntity(), v); }),
				"AddForce", [](RB& c, const glm::vec3& f) { if(PhysicsWorld* p = c.SceneRef->GetPhysicsWorld()) p->AddForce(c.GetEntity(), f); },
				"AddImpulse", [](RB& c, const glm::vec3& f) { if(PhysicsWorld* p = c.SceneRef->GetPhysicsWorld()) p->AddImpulse(c.GetEntity(), f); },
				"AddTorque", [](RB& c, const glm::vec3& f) { if(PhysicsWorld* p = c.SceneRef->GetPhysicsWorld()) p->AddTorque(c.GetEntity(), f); });

			using Col = ComponentRef<ColliderComponent>;
			lua.new_usertype<Col>("Collider", sol::no_constructor,
				"shape", sol::property([](Col& c) { return std::string(ShapeName(c.Get().Shape)); }),
				"size", sol::property([](Col& c) { return c.Get().Size; }),
				"offset", sol::property([](Col& c) { return c.Get().Offset; }),
				"friction", sol::property([](Col& c) { return c.Get().Friction; }),
				"restitution", sol::property([](Col& c) { return c.Get().Restitution; }),
				"isTrigger", sol::property([](Col& c) { return c.Get().IsTrigger; }));

			using AS = ComponentRef<AudioSourceComponent>;
			lua.new_usertype<AS>("AudioSource", sol::no_constructor,
				"clip", sol::property([](AS& c) { return c.Get().Clip; }, [](AS& c, const std::string& v) { c.Get().Clip = v; }),
				"volume", sol::property([](AS& c) { return c.Get().Volume; }, [](AS& c, float v) { if(AudioWorld* a = c.SceneRef->GetAudioWorld()) a->SetVolume(c.GetEntity(), v); else c.Get().Volume = v; }),
				"pitch", sol::property([](AS& c) { return c.Get().Pitch; }, [](AS& c, float v) { if(AudioWorld* a = c.SceneRef->GetAudioWorld()) a->SetPitch(c.GetEntity(), v); else c.Get().Pitch = v; }),
				"loop", sol::property([](AS& c) { return c.Get().Loop; }, [](AS& c, bool v) { c.Get().Loop = v; }),
				"spatial", sol::property([](AS& c) { return c.Get().Spatial; }, [](AS& c, bool v) { c.Get().Spatial = v; }),
				"isPlaying", sol::property([](AS& c) { AudioWorld* a = c.SceneRef->GetAudioWorld(); return a && a->IsPlaying(c.GetEntity()); }),
				"Play", [](AS& c) { AudioWorld* a = c.SceneRef->GetAudioWorld(); return a && a->Play(c.GetEntity()); },
				"Stop", [](AS& c) { if(AudioWorld* a = c.SceneRef->GetAudioWorld()) a->Stop(c.GetEntity()); });

			using AL = ComponentRef<AudioListenerComponent>;
			lua.new_usertype<AL>("AudioListener", sol::no_constructor,
				"active", sol::property([](AL& c) { return c.Get().Active; }, [](AL& c, bool v) { c.Get().Active = v; }));
		}

		void RegisterEntity(ScriptWorld::Impl& impl)
		{
			sol::state& lua = impl.Lua;
			Scene* scene = impl.SceneRef;

			lua.new_usertype<ScriptEntity>("Entity", sol::no_constructor,
				"valid", sol::property([](const ScriptEntity& e) { return static_cast<bool>(e.Resolve()); }),
				"id", sol::property([](const ScriptEntity& e) { return std::to_string(e.ID); }), // string: 64-bit ids do not fit a Lua double
				"name", sol::property(
					[](const ScriptEntity& e) { return Resolve(e).GetName(); },
					[](const ScriptEntity& e, const std::string& name) { Resolve(e).GetComponent<TagComponent>().Tag = name; }),
				"transform", sol::property([scene](const ScriptEntity& e) { Resolve(e); return TransformRef{ scene, e.ID }; }),
				"parent", sol::property(
					[scene](const ScriptEntity& e) -> sol::optional<ScriptEntity> {
						if(Entity parent = Resolve(e).GetParent())
							return ToScript(scene, parent);
						return sol::nullopt;
					},
					[scene](const ScriptEntity& e, sol::optional<ScriptEntity> parent) {
						Entity entity = Resolve(e);
						Entity newParent = parent ? parent->Resolve() : Entity();
						scene->SetParent(entity, newParent, true);
					}),
				"Destroy", [scene](const ScriptEntity& e) { if(Entity entity = e.Resolve()) scene->QueueDestroy(entity); },
				"GetChildren", [scene](const ScriptEntity& e, sol::this_state ts) {
					sol::state_view view(ts);
					sol::table result = view.create_table();
					int i = 1;
					for(Entity child : Resolve(e).GetChildren())
						result[i++] = ToScript(scene, child);
					return result;
				},
				"FindChild", [scene](const ScriptEntity& e, const std::string& name) -> sol::optional<ScriptEntity> {
					for(Entity child : Resolve(e).GetChildren())
						if(child.GetName() == name)
							return ToScript(scene, child);
					return sol::nullopt;
				},
				"HasComponent", [](const ScriptEntity& e, const std::string& type) {
					Entity entity = Resolve(e);
					if(type == "Transform") return true;
					if(type == "Camera") return entity.HasComponent<CameraComponent>();
					if(type == "MeshRenderer") return entity.HasComponent<MeshRendererComponent>();
					if(type == "Light") return entity.HasComponent<LightComponent>();
					if(type == "RigidBody") return entity.HasComponent<RigidBodyComponent>();
					if(type == "Collider") return entity.HasComponent<ColliderComponent>();
					if(type == "AudioSource") return entity.HasComponent<AudioSourceComponent>();
					if(type == "AudioListener") return entity.HasComponent<AudioListenerComponent>();
					if(type == "Script") return entity.HasComponent<ScriptComponent>();
					throw std::runtime_error("unknown component type '" + type + "'");
				},
				"GetComponent", [scene](const ScriptEntity& e, const std::string& type, sol::this_state ts) -> sol::object {
					sol::state_view view(ts);
					Entity entity = Resolve(e);
					if(type == "Transform") return MakeRef<TransformComponent>(view, scene, entity);
					if(type == "Camera") return entity.HasComponent<CameraComponent>() ? MakeRef<CameraComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					if(type == "MeshRenderer") return entity.HasComponent<MeshRendererComponent>() ? MakeRef<MeshRendererComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					if(type == "Light") return entity.HasComponent<LightComponent>() ? MakeRef<LightComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					if(type == "RigidBody") return entity.HasComponent<RigidBodyComponent>() ? MakeRef<RigidBodyComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					if(type == "Collider") return entity.HasComponent<ColliderComponent>() ? MakeRef<ColliderComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					if(type == "AudioSource") return entity.HasComponent<AudioSourceComponent>() ? MakeRef<AudioSourceComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					if(type == "AudioListener") return entity.HasComponent<AudioListenerComponent>() ? MakeRef<AudioListenerComponent>(view, scene, entity) : sol::object(sol::lua_nil);
					throw std::runtime_error("unknown component type '" + type + "'");
				},
				"AddComponent", [scene](const ScriptEntity& e, const std::string& type, sol::this_state ts) -> sol::object {
					sol::state_view view(ts);
					Entity entity = Resolve(e);
					if(type == "Camera") { AddIfMissing<CameraComponent>(entity); return MakeRef<CameraComponent>(view, scene, entity); }
					if(type == "MeshRenderer") { AddIfMissing<MeshRendererComponent>(entity); return MakeRef<MeshRendererComponent>(view, scene, entity); }
					if(type == "Light") { AddIfMissing<LightComponent>(entity); return MakeRef<LightComponent>(view, scene, entity); }
					if(type == "RigidBody") { AddIfMissing<RigidBodyComponent>(entity); return MakeRef<RigidBodyComponent>(view, scene, entity); }
					if(type == "Collider") { AddIfMissing<ColliderComponent>(entity); return MakeRef<ColliderComponent>(view, scene, entity); }
					if(type == "AudioSource") { AddIfMissing<AudioSourceComponent>(entity); return MakeRef<AudioSourceComponent>(view, scene, entity); }
					if(type == "AudioListener") { AddIfMissing<AudioListenerComponent>(entity); return MakeRef<AudioListenerComponent>(view, scene, entity); }
					throw std::runtime_error("cannot add component type '" + type + "' from script");
				},
				"RemoveComponent", [](const ScriptEntity& e, const std::string& type) {
					Entity entity = Resolve(e);
					if(type == "Camera") entity.RemoveComponent<CameraComponent>();
					else if(type == "MeshRenderer") entity.RemoveComponent<MeshRendererComponent>();
					else if(type == "Light") entity.RemoveComponent<LightComponent>();
					else if(type == "AudioSource") entity.RemoveComponent<AudioSourceComponent>();
					else if(type == "AudioListener") entity.RemoveComponent<AudioListenerComponent>();
					else throw std::runtime_error("cannot remove component type '" + type + "' from script");
				},
				"SetScript", [&impl](const ScriptEntity& e, const std::string& path) {
					Entity entity = Resolve(e);
					entity.AddOrReplaceComponent<ScriptComponent>().Script = path;
					impl.CreateInstance(entity);
				},
				"GetScript", [&impl](const ScriptEntity& e) -> sol::object {
					auto it = impl.Instances.find(UUID(e.ID));
					return it == impl.Instances.end() ? sol::object(sol::lua_nil) : sol::object(it->second.Self);
				},
				sol::meta_function::equal_to, [](const ScriptEntity& a, const ScriptEntity& b) { return a.ID == b.ID; },
				sol::meta_function::to_string, [](const ScriptEntity& e) {
					Entity entity = e.Resolve();
					return std::format("Entity({}, '{}')", e.ID, entity ? entity.GetName() : std::string("<destroyed>"));
				});
		}

		void RegisterSceneAPI(ScriptWorld::Impl& impl)
		{
			sol::state& lua = impl.Lua;
			Scene* scene = impl.SceneRef;

			sol::table time = lua.create_named_table("Time");
			time["delta"] = 0.0f;
			time["time"] = 0.0f;
			time["frame"] = 0.0;

			sol::table api = lua.create_named_table("Scene");
			api["Find"] = [scene](const std::string& name) -> sol::optional<ScriptEntity> {
				if(Entity e = scene->FindEntityByName(name))
					return ToScript(scene, e);
				return sol::nullopt;
			};
			api["FindByID"] = [scene](const std::string& idText) -> sol::optional<ScriptEntity> {
				uint64_t id = 0;
				auto [end, ec] = std::from_chars(idText.data(), idText.data() + idText.size(), id);
				if(ec != std::errc() || end != idText.data() + idText.size())
					return sol::nullopt;
				if(Entity e = scene->FindEntityByUUID(UUID(id)))
					return ToScript(scene, e);
				return sol::nullopt;
			};
			api["Create"] = [scene](sol::optional<std::string> name) {
				return ToScript(scene, scene->CreateEntity(name.value_or("Entity")));
			};
			api["Instantiate"] = [scene, &impl](const std::string& prefab, sol::optional<glm::vec3> position, sol::optional<ScriptEntity> parent) -> sol::optional<ScriptEntity> {
				Entity parentEntity = parent ? parent->Resolve() : Entity();
				glm::vec3 pos = position.value_or(glm::vec3(0.0f));
				Entity e = scene->InstantiatePrefab(prefab, position ? &pos : nullptr, parentEntity);
				if(!e)
					return sol::nullopt;
				ScriptInstantiateSubtree(impl, e);
				return ToScript(scene, e);
			};
			api["GetMainCamera"] = [scene]() -> sol::optional<ScriptEntity> {
				if(Entity e = scene->GetPrimaryCameraEntity())
					return ToScript(scene, e);
				return sol::nullopt;
			};
			api["GetEntities"] = [scene](sol::this_state ts) {
				sol::state_view view(ts);
				sol::table result = view.create_table();
				int i = 1;
				for(Entity e : scene->GetEntities())
					result[i++] = ToScript(scene, e);
				return result;
			};
			api["Raycast"] = [scene](const glm::vec3& origin, const glm::vec3& direction, sol::optional<float> maxDistance, sol::this_state ts) -> sol::object {
				sol::state_view view(ts);
				PhysicsWorld* physics = scene->GetPhysicsWorld();
				RaycastHit hit;
				if(!physics || !physics->Raycast(origin, direction, maxDistance.value_or(1000.0f), hit))
					return sol::object(sol::lua_nil);
				sol::table result = view.create_table();
				result["entity"] = ToScript(scene, scene->FindEntityByUUID(hit.HitEntity));
				result["point"] = hit.Point;
				result["normal"] = hit.Normal;
				result["distance"] = hit.Distance;
				return result;
			};
			api["OverlapSphere"] = [scene](const glm::vec3& center, float radius, sol::this_state ts) {
				sol::state_view view(ts);
				sol::table result = view.create_table();
				if(PhysicsWorld* physics = scene->GetPhysicsWorld())
				{
					int i = 1;
					for(UUID id : physics->OverlapSphere(center, radius))
						if(Entity e = scene->FindEntityByUUID(id))
							result[i++] = ToScript(scene, e);
				}
				return result;
			};
			api["SetGravity"] = [scene](const glm::vec3& g) {
				scene->GetSettings().Gravity = g;
				if(PhysicsWorld* physics = scene->GetPhysicsWorld())
					physics->SetGravity(g);
			};
			api["GetGravity"] = [scene]() { return scene->GetSettings().Gravity; };
		}

		void RegisterPlatformAPI(sol::state& lua, Scene* scene)
		{
			auto key = [](const std::string& name) {
				KeyCode code = KeyCodeFromString(name);
				if(code == KeyCode::Unknown)
					throw std::runtime_error("unknown key '" + name + "'");
				return code;
			};
			auto button = [](int b) {
				if(b < 0 || b > 2)
					throw std::runtime_error("mouse button must be 0 (left), 1 (right) or 2 (middle)");
				return static_cast<MouseButton>(b);
			};

			sol::table input = lua.create_named_table("Input");
			input["IsKeyDown"] = [key](const std::string& n) { return Input::IsKeyDown(key(n)); };
			input["IsKeyPressed"] = [key](const std::string& n) { return Input::IsKeyPressed(key(n)); };
			input["IsKeyReleased"] = [key](const std::string& n) { return Input::IsKeyReleased(key(n)); };
			input["IsMouseButtonDown"] = [button](int b) { return Input::IsMouseButtonDown(button(b)); };
			input["IsMouseButtonPressed"] = [button](int b) { return Input::IsMouseButtonPressed(button(b)); };
			input["IsMouseButtonReleased"] = [button](int b) { return Input::IsMouseButtonReleased(button(b)); };
			input["GetMousePosition"] = []() { glm::vec2 p = Input::GetMousePosition(); return std::make_tuple(p.x, p.y); };
			input["GetMouseDelta"] = []() { glm::vec2 p = Input::GetMouseDelta(); return std::make_tuple(p.x, p.y); };
			input["GetScroll"] = []() { return Input::GetScrollDelta().y; };

			sol::table audio = lua.create_named_table("Audio");
			audio["PlayOneShot"] = [scene](const std::string& clip, sol::optional<glm::vec3> position, sol::optional<float> volume) {
				AudioWorld* world = scene->GetAudioWorld();
				return world && world->PlayOneShot(clip, position.value_or(glm::vec3(0.0f)), volume.value_or(1.0f), position.has_value());
			};
			audio["SetMasterVolume"] = [](float v) { AudioEngine::SetMasterVolume(v); };
			audio["GetMasterVolume"] = []() { return AudioEngine::GetMasterVolume(); };

			sol::table log = lua.create_named_table("Log");
			auto join = [](sol::variadic_args args, sol::this_state ts) {
				sol::state_view view(ts);
				std::string message;
				for(auto arg : args)
				{
					if(!message.empty())
						message += ' ';
					message += view["tostring"](arg).get<std::string>();
				}
				return message;
			};
			log["Info"] = [join](sol::variadic_args a, sol::this_state ts) { SF_INFO("{0}", join(a, ts)); };
			log["Warn"] = [join](sol::variadic_args a, sol::this_state ts) { SF_WARN("{0}", join(a, ts)); };
			log["Error"] = [join](sol::variadic_args a, sol::this_state ts) { SF_ERROR("{0}", join(a, ts)); };
			lua["print"] = [join](sol::variadic_args a, sol::this_state ts) { SF_INFO("{0}", join(a, ts)); };

			sol::table app = lua.create_named_table("Application");
			app["Quit"] = []() { ScriptRequestQuit(); };
		}

	}

	void RegisterScriptAPI(ScriptWorld::Impl& impl)
	{
		RegisterMath(impl.Lua);
		RegisterComponents(impl.Lua);
		RegisterEntity(impl);
		RegisterSceneAPI(impl);
		RegisterPlatformAPI(impl.Lua, impl.SceneRef);
	}

}
