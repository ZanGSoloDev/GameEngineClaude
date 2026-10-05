#pragma once

#include "Starfall/Core/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Starfall {

	// Assets are referenced by project-relative path (forward slashes), e.g. "Models/Helmet.gltf".
	// Built-in primitives use the "builtin://" scheme, e.g. "builtin://Cube".
	using AssetPath = std::string;

	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		explicit IDComponent(UUID id) : ID(id) {}
	};

	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		explicit TagComponent(std::string tag) : Tag(std::move(tag)) {}
	};

	// Local transform. Rotation is stored as Euler angles (radians, XYZ order).
	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		TransformComponent() = default;
		explicit TransformComponent(const glm::vec3& translation) : Translation(translation) {}

		glm::quat GetRotationQuat() const { return glm::quat(Rotation); }
		void SetRotationQuat(const glm::quat& q) { Rotation = glm::eulerAngles(q); }

		glm::mat4 GetTransform() const
		{
			return glm::translate(glm::mat4(1.0f), Translation) * glm::toMat4(GetRotationQuat()) * glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	// Parent/children links stored by UUID so they survive serialization and copying.
	struct RelationshipComponent
	{
		UUID Parent = UUID(0);
		std::vector<UUID> Children;
	};

	enum class ProjectionType { Perspective = 0, Orthographic = 1 };

	struct CameraComponent
	{
		ProjectionType Projection = ProjectionType::Perspective;
		float VerticalFOV = glm::radians(45.0f);
		float PerspectiveNear = 0.1f;
		float PerspectiveFar = 500.0f;
		float OrthographicSize = 10.0f;
		float OrthographicNear = -50.0f;
		float OrthographicFar = 50.0f;
		bool Primary = true;

		glm::mat4 GetProjection(float aspectRatio) const;
	};

	struct MeshRendererComponent
	{
		AssetPath Mesh = "builtin://Cube";    // "builtin://X" or "Models/File.gltf" (all meshes) or "Models/File.gltf#2" (single mesh)
		AssetPath Material;                   // empty = use the mesh's own material; otherwise "Materials/X.sfmat"
		bool CastShadows = true;
		bool ReceiveShadows = true;
		bool Visible = true;
	};

	enum class LightType { Directional = 0, Point = 1, Spot = 2 };

	struct LightComponent
	{
		LightType Type = LightType::Point;
		glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;          // lux for directional, candela-ish for point/spot
		float Range = 15.0f;             // point/spot
		float InnerConeAngle = glm::radians(20.0f);
		float OuterConeAngle = glm::radians(30.0f);
		bool CastShadows = true;
		float ShadowSoftness = 1.0f;     // PCSS light size scale
		float ShadowBias = 0.002f;
	};

	enum class BodyType { Static = 0, Kinematic = 1, Dynamic = 2 };

	struct RigidBodyComponent
	{
		BodyType Type = BodyType::Dynamic;
		float Mass = 1.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityFactor = 1.0f;
		bool LockRotationX = false;
		bool LockRotationY = false;
		bool LockRotationZ = false;
		bool Continuous = false; // continuous collision detection (fast moving bodies)

		// Runtime only (not serialized): physics body id.
		uint32_t RuntimeBodyID = 0xFFFFFFFFu;
	};

	enum class ColliderShape { Box = 0, Sphere = 1, Capsule = 2 };

	struct ColliderComponent
	{
		ColliderShape Shape = ColliderShape::Box;
		glm::vec3 Size = { 1.0f, 1.0f, 1.0f }; // box: full extents; sphere: x = diameter; capsule: x = diameter, y = total height
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
		float Friction = 0.5f;
		float Restitution = 0.0f;
		bool IsTrigger = false;
	};

	struct AudioSourceComponent
	{
		AssetPath Clip;
		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool Loop = false;
		bool PlayOnStart = false;
		bool Spatial = true;
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;

		// Runtime only.
		uint32_t RuntimeSoundID = 0xFFFFFFFFu;
	};

	struct AudioListenerComponent
	{
		bool Active = true;
	};

	using ScriptPropertyValue = std::variant<bool, double, std::string>;

	struct ScriptComponent
	{
		AssetPath Script;                                             // "Scripts/Foo.lua"
		std::unordered_map<std::string, ScriptPropertyValue> Properties; // overrides for the script's exported properties

		// Runtime only: reference into the Lua state (0 = not instantiated).
		int RuntimeInstanceRef = 0;
	};

	// Compile-time list of every serializable/editor-visible component (excluding ID/Tag/Relationship).
	template<typename... Components>
	struct ComponentGroup {};

	using AllComponents = ComponentGroup<TransformComponent, CameraComponent, MeshRendererComponent, LightComponent, RigidBodyComponent,
		ColliderComponent, AudioSourceComponent, AudioListenerComponent, ScriptComponent>;

}
