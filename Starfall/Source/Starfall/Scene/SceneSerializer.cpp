#include "Starfall/Scene/SceneSerializer.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Scene/Entity.h"

#include <nlohmann/json.hpp>

namespace Starfall {

	using json = nlohmann::json;

	namespace {

		json ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }

		glm::vec3 ToVec3(const json& j, const glm::vec3& fallback)
		{
			if(!j.is_array() || j.size() != 3)
				return fallback;
			for(const json& v : j)
				if(!v.is_number())
					return fallback;
			return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
		}

		template<typename T>
		T ToEnum(const json& j, const char* key, T fallback, int maxValue)
		{
			int value = j.value(key, static_cast<int>(fallback));
			if(value < 0 || value > maxValue)
				return fallback;
			return static_cast<T>(value);
		}

		json SerializeComponent(const TransformComponent& c)
		{
			return { { "Translation", ToJson(c.Translation) }, { "Rotation", ToJson(c.Rotation) }, { "Scale", ToJson(c.Scale) } };
		}
		void DeserializeComponent(const json& j, TransformComponent& c)
		{
			c.Translation = ToVec3(j.value("Translation", json()), c.Translation);
			c.Rotation = ToVec3(j.value("Rotation", json()), c.Rotation);
			c.Scale = ToVec3(j.value("Scale", json()), c.Scale);
		}

		json SerializeComponent(const CameraComponent& c)
		{
			return { { "Projection", static_cast<int>(c.Projection) }, { "VerticalFOV", c.VerticalFOV }, { "PerspectiveNear", c.PerspectiveNear },
				{ "PerspectiveFar", c.PerspectiveFar }, { "OrthographicSize", c.OrthographicSize }, { "OrthographicNear", c.OrthographicNear },
				{ "OrthographicFar", c.OrthographicFar }, { "Primary", c.Primary } };
		}
		void DeserializeComponent(const json& j, CameraComponent& c)
		{
			c.Projection = ToEnum(j, "Projection", c.Projection, 1);
			c.VerticalFOV = j.value("VerticalFOV", c.VerticalFOV);
			c.PerspectiveNear = j.value("PerspectiveNear", c.PerspectiveNear);
			c.PerspectiveFar = j.value("PerspectiveFar", c.PerspectiveFar);
			c.OrthographicSize = j.value("OrthographicSize", c.OrthographicSize);
			c.OrthographicNear = j.value("OrthographicNear", c.OrthographicNear);
			c.OrthographicFar = j.value("OrthographicFar", c.OrthographicFar);
			c.Primary = j.value("Primary", c.Primary);
		}

		json SerializeComponent(const MeshRendererComponent& c)
		{
			return { { "Mesh", c.Mesh }, { "Material", c.Material }, { "CastShadows", c.CastShadows }, { "ReceiveShadows", c.ReceiveShadows }, { "Visible", c.Visible } };
		}
		void DeserializeComponent(const json& j, MeshRendererComponent& c)
		{
			c.Mesh = j.value("Mesh", c.Mesh);
			c.Material = j.value("Material", c.Material);
			c.CastShadows = j.value("CastShadows", c.CastShadows);
			c.ReceiveShadows = j.value("ReceiveShadows", c.ReceiveShadows);
			c.Visible = j.value("Visible", c.Visible);
		}

		json SerializeComponent(const LightComponent& c)
		{
			return { { "Type", static_cast<int>(c.Type) }, { "Color", ToJson(c.Color) }, { "Intensity", c.Intensity }, { "Range", c.Range },
				{ "InnerConeAngle", c.InnerConeAngle }, { "OuterConeAngle", c.OuterConeAngle }, { "CastShadows", c.CastShadows },
				{ "ShadowSoftness", c.ShadowSoftness }, { "ShadowBias", c.ShadowBias } };
		}
		void DeserializeComponent(const json& j, LightComponent& c)
		{
			c.Type = ToEnum(j, "Type", c.Type, 2);
			c.Color = ToVec3(j.value("Color", json()), c.Color);
			c.Intensity = j.value("Intensity", c.Intensity);
			c.Range = j.value("Range", c.Range);
			c.InnerConeAngle = j.value("InnerConeAngle", c.InnerConeAngle);
			c.OuterConeAngle = j.value("OuterConeAngle", c.OuterConeAngle);
			c.CastShadows = j.value("CastShadows", c.CastShadows);
			c.ShadowSoftness = j.value("ShadowSoftness", c.ShadowSoftness);
			c.ShadowBias = j.value("ShadowBias", c.ShadowBias);
		}

		json SerializeComponent(const RigidBodyComponent& c)
		{
			return { { "Type", static_cast<int>(c.Type) }, { "Mass", c.Mass }, { "LinearDamping", c.LinearDamping }, { "AngularDamping", c.AngularDamping },
				{ "GravityFactor", c.GravityFactor }, { "LockRotationX", c.LockRotationX }, { "LockRotationY", c.LockRotationY },
				{ "LockRotationZ", c.LockRotationZ }, { "Continuous", c.Continuous } };
		}
		void DeserializeComponent(const json& j, RigidBodyComponent& c)
		{
			c.Type = ToEnum(j, "Type", c.Type, 2);
			c.Mass = j.value("Mass", c.Mass);
			c.LinearDamping = j.value("LinearDamping", c.LinearDamping);
			c.AngularDamping = j.value("AngularDamping", c.AngularDamping);
			c.GravityFactor = j.value("GravityFactor", c.GravityFactor);
			c.LockRotationX = j.value("LockRotationX", c.LockRotationX);
			c.LockRotationY = j.value("LockRotationY", c.LockRotationY);
			c.LockRotationZ = j.value("LockRotationZ", c.LockRotationZ);
			c.Continuous = j.value("Continuous", c.Continuous);
		}

		json SerializeComponent(const ColliderComponent& c)
		{
			return { { "Shape", static_cast<int>(c.Shape) }, { "Size", ToJson(c.Size) }, { "Offset", ToJson(c.Offset) }, { "Friction", c.Friction },
				{ "Restitution", c.Restitution }, { "IsTrigger", c.IsTrigger } };
		}
		void DeserializeComponent(const json& j, ColliderComponent& c)
		{
			c.Shape = ToEnum(j, "Shape", c.Shape, 2);
			c.Size = ToVec3(j.value("Size", json()), c.Size);
			c.Offset = ToVec3(j.value("Offset", json()), c.Offset);
			c.Friction = j.value("Friction", c.Friction);
			c.Restitution = j.value("Restitution", c.Restitution);
			c.IsTrigger = j.value("IsTrigger", c.IsTrigger);
		}

		json SerializeComponent(const AudioSourceComponent& c)
		{
			return { { "Clip", c.Clip }, { "Volume", c.Volume }, { "Pitch", c.Pitch }, { "Loop", c.Loop }, { "PlayOnStart", c.PlayOnStart },
				{ "Spatial", c.Spatial }, { "MinDistance", c.MinDistance }, { "MaxDistance", c.MaxDistance } };
		}
		void DeserializeComponent(const json& j, AudioSourceComponent& c)
		{
			c.Clip = j.value("Clip", c.Clip);
			c.Volume = j.value("Volume", c.Volume);
			c.Pitch = j.value("Pitch", c.Pitch);
			c.Loop = j.value("Loop", c.Loop);
			c.PlayOnStart = j.value("PlayOnStart", c.PlayOnStart);
			c.Spatial = j.value("Spatial", c.Spatial);
			c.MinDistance = j.value("MinDistance", c.MinDistance);
			c.MaxDistance = j.value("MaxDistance", c.MaxDistance);
		}

		json SerializeComponent(const AudioListenerComponent& c) { return { { "Active", c.Active } }; }
		void DeserializeComponent(const json& j, AudioListenerComponent& c) { c.Active = j.value("Active", c.Active); }

		json SerializeComponent(const ScriptComponent& c)
		{
			json props = json::object();
			for(const auto& [key, value] : c.Properties)
				std::visit([&](const auto& v) { props[key] = v; }, value);
			return { { "Script", c.Script }, { "Properties", props } };
		}
		void DeserializeComponent(const json& j, ScriptComponent& c)
		{
			c.Script = j.value("Script", c.Script);
			c.Properties.clear();
			if(j.contains("Properties") && j["Properties"].is_object())
			{
				for(const auto& [key, value] : j["Properties"].items())
				{
					if(value.is_boolean())
						c.Properties[key] = value.get<bool>();
					else if(value.is_number())
						c.Properties[key] = value.get<double>();
					else if(value.is_string())
						c.Properties[key] = value.get<std::string>();
				}
			}
		}

		const char* ComponentName(const TransformComponent*) { return "TransformComponent"; }
		const char* ComponentName(const CameraComponent*) { return "CameraComponent"; }
		const char* ComponentName(const MeshRendererComponent*) { return "MeshRendererComponent"; }
		const char* ComponentName(const LightComponent*) { return "LightComponent"; }
		const char* ComponentName(const RigidBodyComponent*) { return "RigidBodyComponent"; }
		const char* ComponentName(const ColliderComponent*) { return "ColliderComponent"; }
		const char* ComponentName(const AudioSourceComponent*) { return "AudioSourceComponent"; }
		const char* ComponentName(const AudioListenerComponent*) { return "AudioListenerComponent"; }
		const char* ComponentName(const ScriptComponent*) { return "ScriptComponent"; }

		template<typename... Components>
		void SerializeComponents(ComponentGroup<Components...>, Entity entity, json& out)
		{
			([&] {
				if(Components* component = entity.TryGetComponent<Components>())
					out[ComponentName(static_cast<const Components*>(nullptr))] = SerializeComponent(*component);
			}(), ...);
		}

		template<typename... Components>
		void DeserializeComponents(ComponentGroup<Components...>, const json& in, Entity entity)
		{
			([&] {
				const char* name = ComponentName(static_cast<const Components*>(nullptr));
				if(in.contains(name) && in[name].is_object())
				{
					Components& component = entity.AddOrReplaceComponent<Components>();
					DeserializeComponent(in[name], component);
				}
			}(), ...);
		}

		json SerializeEntity(Entity entity)
		{
			json j;
			j["UUID"] = static_cast<uint64_t>(entity.GetUUID());
			j["Name"] = entity.GetName();
			j["Parent"] = static_cast<uint64_t>(entity.GetComponent<RelationshipComponent>().Parent);
			json components = json::object();
			SerializeComponents(AllComponents{}, entity, components);
			j["Components"] = std::move(components);
			return j;
		}

		json SerializeSettings(const SceneSettings& s)
		{
			return {
				{ "Environment", { { "HDRI", s.Environment.HDRI }, { "Intensity", s.Environment.Intensity }, { "RotationDegrees", s.Environment.RotationDegrees },
					{ "ShowSkybox", s.Environment.ShowSkybox }, { "SkyColor", ToJson(s.Environment.SkyColor) }, { "GroundColor", ToJson(s.Environment.GroundColor) } } },
				{ "PostProcess", { { "Exposure", s.PostProcess.Exposure }, { "Tonemapper", static_cast<int>(s.PostProcess.TonemapMode) },
					{ "SSAOEnabled", s.PostProcess.SSAOEnabled }, { "SSAORadius", s.PostProcess.SSAORadius }, { "SSAOIntensity", s.PostProcess.SSAOIntensity },
					{ "SSAOSamples", s.PostProcess.SSAOSamples } } },
				{ "Shadows", { { "MapSize", s.Shadows.MapSize }, { "MaxDistance", s.Shadows.MaxDistance }, { "CascadeCount", s.Shadows.CascadeCount },
					{ "CascadeSplitLambda", s.Shadows.CascadeSplitLambda } } },
				{ "Gravity", ToJson(s.Gravity) }
			};
		}

		void DeserializeSettings(const json& j, SceneSettings& s)
		{
			if(auto it = j.find("Environment"); it != j.end() && it->is_object())
			{
				const json& e = *it;
				s.Environment.HDRI = e.value("HDRI", s.Environment.HDRI);
				s.Environment.Intensity = e.value("Intensity", s.Environment.Intensity);
				s.Environment.RotationDegrees = e.value("RotationDegrees", s.Environment.RotationDegrees);
				s.Environment.ShowSkybox = e.value("ShowSkybox", s.Environment.ShowSkybox);
				s.Environment.SkyColor = ToVec3(e.value("SkyColor", json()), s.Environment.SkyColor);
				s.Environment.GroundColor = ToVec3(e.value("GroundColor", json()), s.Environment.GroundColor);
			}
			if(auto it = j.find("PostProcess"); it != j.end() && it->is_object())
			{
				const json& p = *it;
				s.PostProcess.Exposure = p.value("Exposure", s.PostProcess.Exposure);
				s.PostProcess.TonemapMode = ToEnum(p, "Tonemapper", s.PostProcess.TonemapMode, 3);
				s.PostProcess.SSAOEnabled = p.value("SSAOEnabled", s.PostProcess.SSAOEnabled);
				s.PostProcess.SSAORadius = p.value("SSAORadius", s.PostProcess.SSAORadius);
				s.PostProcess.SSAOIntensity = p.value("SSAOIntensity", s.PostProcess.SSAOIntensity);
				s.PostProcess.SSAOSamples = p.value("SSAOSamples", s.PostProcess.SSAOSamples);
			}
			if(auto it = j.find("Shadows"); it != j.end() && it->is_object())
			{
				const json& sh = *it;
				s.Shadows.MapSize = sh.value("MapSize", s.Shadows.MapSize);
				s.Shadows.MaxDistance = sh.value("MaxDistance", s.Shadows.MaxDistance);
				s.Shadows.CascadeCount = sh.value("CascadeCount", s.Shadows.CascadeCount);
				s.Shadows.CascadeSplitLambda = sh.value("CascadeSplitLambda", s.Shadows.CascadeSplitLambda);
			}
			s.Gravity = ToVec3(j.value("Gravity", json()), s.Gravity);
		}

		// Creates the entities in `entities`. If `remap` is true all UUIDs are regenerated (prefab instantiation).
		// Returns the created entities in file order. Invalid parent links become roots.
		std::vector<Entity> DeserializeEntities(Scene& scene, const json& entities, bool remap)
		{
			std::vector<Entity> created;
			std::unordered_map<uint64_t, UUID> idMap;
			std::vector<std::pair<Entity, uint64_t>> parentLinks;

			for(const json& je : entities)
			{
				if(!je.is_object() || !je.contains("UUID") || !je["UUID"].is_number_unsigned())
					continue;
				uint64_t fileId = je["UUID"].get<uint64_t>();
				if(fileId == 0 || idMap.contains(fileId))
					continue;
				UUID id = remap ? UUID() : UUID(fileId);
				if(!remap && scene.FindEntityByUUID(id))
					continue;
				idMap[fileId] = id;
				Entity entity = scene.CreateEntityWithUUID(id, je.value("Name", "Entity"));
				if(je.contains("Components") && je["Components"].is_object())
					DeserializeComponents(AllComponents{}, je["Components"], entity);
				uint64_t parentId = je.contains("Parent") && je["Parent"].is_number_unsigned() ? je["Parent"].get<uint64_t>() : 0;
				parentLinks.emplace_back(entity, parentId);
				created.push_back(entity);
			}

			for(auto& [entity, parentFileId] : parentLinks)
			{
				auto it = idMap.find(parentFileId);
				if(it == idMap.end())
					continue;
				Entity parent = scene.FindEntityByUUID(it->second);
				if(parent && parent != entity)
					scene.SetParent(entity, parent, false);
			}
			return created;
		}

	}

	std::string SceneSerializer::SerializeToString(const Scene& scene)
	{
		Scene& s = const_cast<Scene&>(scene);
		json j;
		j["Version"] = FormatVersion;
		j["Type"] = "Scene";
		j["Name"] = s.GetName();
		j["Settings"] = SerializeSettings(s.GetSettings());
		j["Entities"] = json::array();
		for(Entity entity : s.GetEntities())
			j["Entities"].push_back(SerializeEntity(entity));
		return j.dump(2);
	}

	bool SceneSerializer::DeserializeFromString(Scene& scene, const std::string& text)
	{
		json j = json::parse(text, nullptr, false);
		if(j.is_discarded() || !j.is_object() || j.value("Type", "") != "Scene" || !j.contains("Entities") || !j["Entities"].is_array())
		{
			SF_CORE_ERROR("Malformed scene data");
			return false;
		}
		if(j.value("Version", 0) > FormatVersion)
		{
			SF_CORE_ERROR("Scene was saved by a newer engine version");
			return false;
		}

		for(Entity entity : scene.GetRootEntities())
			scene.DestroyEntity(entity);
		scene.SetName(j.value("Name", "Untitled"));
		scene.GetSettings() = SceneSettings{};
		if(j.contains("Settings") && j["Settings"].is_object())
			DeserializeSettings(j["Settings"], scene.GetSettings());
		DeserializeEntities(scene, j["Entities"], false);
		return true;
	}

	bool SceneSerializer::Serialize(const Scene& scene, const std::filesystem::path& file)
	{
		return FileSystem::WriteText(file, SerializeToString(scene));
	}

	bool SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& file)
	{
		auto text = FileSystem::ReadText(file);
		if(!text)
		{
			SF_CORE_ERROR("Could not read scene '{0}'", file.string());
			return false;
		}
		return DeserializeFromString(scene, *text);
	}

	std::string SceneSerializer::SerializePrefabToString(Scene& scene, Entity root)
	{
		(void)scene;
		json j;
		j["Version"] = FormatVersion;
		j["Type"] = "Prefab";
		j["Root"] = static_cast<uint64_t>(root.GetUUID());
		j["Entities"] = json::array();
		// Breadth-first so the root is always first and parents precede children.
		std::vector<Entity> queue{ root };
		for(size_t i = 0; i < queue.size(); i++)
		{
			Entity current = queue[i];
			json je = SerializeEntity(current);
			if(current == root)
				je["Parent"] = uint64_t(0);
			j["Entities"].push_back(std::move(je));
			for(Entity child : current.GetChildren())
				queue.push_back(child);
		}
		return j.dump(2);
	}

	bool SceneSerializer::SerializePrefab(Scene& scene, Entity root, const std::filesystem::path& file)
	{
		if(!root)
			return false;
		return FileSystem::WriteText(file, SerializePrefabToString(scene, root));
	}

	Entity SceneSerializer::DeserializePrefabFromString(Scene& scene, const std::string& text)
	{
		json j = json::parse(text, nullptr, false);
		if(j.is_discarded() || !j.is_object() || j.value("Type", "") != "Prefab" || !j.contains("Entities") || !j["Entities"].is_array())
		{
			SF_CORE_ERROR("Malformed prefab data");
			return {};
		}
		std::vector<Entity> created = DeserializeEntities(scene, j["Entities"], true);
		if(created.empty())
			return {};
		return created.front(); // root is serialized first
	}

	Entity SceneSerializer::DeserializePrefab(Scene& scene, const AssetPath& assetPath)
	{
		std::filesystem::path file = Project::ResolvePath(assetPath);
		auto text = file.empty() ? std::nullopt : FileSystem::ReadText(file);
		if(!text)
		{
			SF_CORE_ERROR("Could not read prefab '{0}'", assetPath);
			return {};
		}
		return DeserializePrefabFromString(scene, *text);
	}

}
