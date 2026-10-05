#include "EditorActions.h"

#include "AssetFiles.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Renderer/Model.h"
#include "Starfall/Scene/SceneSerializer.h"

#include <glm/gtc/constants.hpp>

namespace StarfallEditor::Actions {

	namespace {

		void Finish(EditorContext& ctx, Entity e, Entity parent, const std::string& label)
		{
			if(parent && e)
				ctx.GetScene().SetParent(e, parent, false);
			ctx.Select(e);
			if(ctx.Commit)
				ctx.Commit(label);
		}

	}

	std::string UniqueName(Scene& scene, const std::string& base)
	{
		if(!scene.FindEntityByName(base))
			return base;
		for(int i = 1; i < 100000; i++)
		{
			std::string candidate = base + " (" + std::to_string(i) + ")";
			if(!scene.FindEntityByName(candidate))
				return candidate;
		}
		return base;
	}

	Entity CreateEmpty(EditorContext& ctx, Entity parent)
	{
		Entity e = ctx.GetScene().CreateEntity(UniqueName(ctx.GetScene(), "Empty"));
		Finish(ctx, e, parent, "Create Empty");
		return e;
	}

	Entity CreateMesh(EditorContext& ctx, const std::string& name, const std::string& mesh, Entity parent)
	{
		Entity e = ctx.GetScene().CreateEntity(UniqueName(ctx.GetScene(), name));
		e.AddComponent<MeshRendererComponent>().Mesh = mesh;
		Finish(ctx, e, parent, "Create " + name);
		return e;
	}

	Entity CreateLight(EditorContext& ctx, LightType type, Entity parent)
	{
		const char* names[] = { "Directional Light", "Point Light", "Spot Light" };
		Entity e = ctx.GetScene().CreateEntity(UniqueName(ctx.GetScene(), names[static_cast<int>(type)]));
		auto& light = e.AddComponent<LightComponent>();
		light.Type = type;
		if(type == LightType::Directional)
		{
			light.Intensity = 3.0f;
			e.Transform().Rotation = { glm::radians(-50.0f), glm::radians(30.0f), 0.0f };
			e.Transform().Translation = { 0, 5, 0 };
		}
		else
		{
			light.Intensity = type == LightType::Point ? 30.0f : 80.0f;
			e.Transform().Translation = { 0, 3, 0 };
			if(type == LightType::Spot)
				e.Transform().Rotation = { glm::radians(-90.0f), 0.0f, 0.0f };
		}
		Finish(ctx, e, parent, "Create Light");
		return e;
	}

	Entity CreateCamera(EditorContext& ctx, Entity parent)
	{
		Entity e = ctx.GetScene().CreateEntity(UniqueName(ctx.GetScene(), "Camera"));
		e.AddComponent<CameraComponent>().Primary = !ctx.GetScene().GetPrimaryCameraEntity();
		e.Transform().Translation = { 0, 2, 8 };
		Finish(ctx, e, parent, "Create Camera");
		return e;
	}

	Entity CreateAudioSource(EditorContext& ctx, Entity parent)
	{
		Entity e = ctx.GetScene().CreateEntity(UniqueName(ctx.GetScene(), "Audio Source"));
		e.AddComponent<AudioSourceComponent>();
		Finish(ctx, e, parent, "Create Audio Source");
		return e;
	}

	Entity Duplicate(EditorContext& ctx, Entity entity)
	{
		if(!entity)
			return {};
		Entity copy = ctx.GetScene().DuplicateEntity(entity);
		if(copy)
			copy.GetComponent<TagComponent>().Tag = UniqueName(ctx.GetScene(), entity.GetName());
		ctx.Select(copy);
		if(ctx.Commit)
			ctx.Commit("Duplicate");
		return copy;
	}

	void Delete(EditorContext& ctx, Entity entity)
	{
		if(!entity)
			return;
		if(ctx.Selected == entity.GetUUID())
			ctx.Selected = UUID(0);
		if(ctx.IsPlaying())
			ctx.GetScene().QueueDestroy(entity);
		else
			ctx.GetScene().DestroyEntity(entity);
		if(ctx.Commit)
			ctx.Commit("Delete");
	}

	void Reparent(EditorContext& ctx, Entity child, Entity newParent)
	{
		if(!child || child == newParent)
			return;
		if(newParent && ctx.GetScene().IsDescendantOf(newParent, child))
			return;
		ctx.GetScene().SetParent(child, newParent, true);
		if(ctx.Commit)
			ctx.Commit("Reparent");
	}

	Entity Instantiate(EditorContext& ctx, const AssetPath& asset, Entity parent)
	{
		AssetKind kind = ClassifyAsset(asset, false);
		Entity root;
		if(kind == AssetKind::Prefab)
			root = ctx.GetScene().InstantiatePrefab(asset);
		else if(kind == AssetKind::Model)
			root = InstantiateModel(ctx.GetScene(), asset, {});
		if(!root)
		{
			if(ctx.ShowToast)
				ctx.ShowToast("Could not instantiate '" + asset + "'");
			return {};
		}
		Finish(ctx, root, parent, "Instantiate " + root.GetName());
		return root;
	}

	AssetPath SaveAsPrefab(EditorContext& ctx, Entity entity)
	{
		if(!entity || !Project::IsLoaded())
			return {};
		std::string name = entity.GetName();
		for(char& c : name)
			if(!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' '))
				c = '_';
		AssetPath path = "Prefabs/" + name + ".sfprefab";
		if(!SceneSerializer::SerializePrefab(ctx.GetScene(), entity, Project::ResolvePath(path)))
			return {};
		return path;
	}

	bool AssignAsset(EditorContext& ctx, Entity entity, const AssetPath& asset)
	{
		if(!entity)
			return false;
		AssetKind kind = ClassifyAsset(asset, false);
		bool applied = false;
		switch(kind)
		{
			case AssetKind::Material:
				if(auto* mr = entity.TryGetComponent<MeshRendererComponent>()) { mr->Material = asset; applied = true; }
				break;
			case AssetKind::Model:
				if(AssetManager::GetModel(asset))
				{
					entity.AddOrReplaceComponent<MeshRendererComponent>().Mesh = asset + "#0";
					applied = true;
				}
				break;
			case AssetKind::Script:
				entity.AddOrReplaceComponent<ScriptComponent>().Script = asset;
				applied = true;
				break;
			case AssetKind::Audio:
				entity.AddOrReplaceComponent<AudioSourceComponent>().Clip = asset;
				applied = true;
				break;
			default:
				break;
		}
		if(applied && ctx.Commit)
			ctx.Commit("Assign " + asset);
		return applied;
	}

	void GetBounds(Scene& scene, Entity entity, glm::vec3& center, float& radius)
	{
		Math::AABB bounds;
		std::vector<Entity> stack{ entity };
		while(!stack.empty())
		{
			Entity current = stack.back();
			stack.pop_back();
			glm::mat4 world = scene.GetWorldTransform(current);
			if(auto* mr = current.TryGetComponent<MeshRendererComponent>())
				if(Ref<Mesh> mesh = AssetManager::GetMesh(mr->Mesh))
					bounds.Expand(mesh->GetBounds().Transformed(world));
			bounds.Expand(glm::vec3(world[3]));
			for(Entity child : current.GetChildren())
				stack.push_back(child);
		}
		center = bounds.Center();
		radius = glm::length(bounds.Extents());
		if(!bounds.IsValid() || radius < 0.01f)
			radius = 1.0f;
	}

}
