#include "Starfall/Scene/Scene.h"

#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Math/MathUtils.h"
#include "Starfall/Physics/PhysicsWorld.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"
#include "Starfall/Scripting/ScriptWorld.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Starfall {

	glm::mat4 CameraComponent::GetProjection(float aspectRatio) const
	{
		glm::mat4 projection; // Right-handed, depth 0..1. nvrhi flips the Vulkan viewport, so clip space is Y-up like D3D/GL.
		if(Projection == ProjectionType::Perspective)
		{
			projection = glm::perspective(VerticalFOV, aspectRatio, PerspectiveNear, PerspectiveFar);
		}
		else
		{
			float orthoLeft = -OrthographicSize * aspectRatio * 0.5f;
			float orthoRight = OrthographicSize * aspectRatio * 0.5f;
			float orthoBottom = -OrthographicSize * 0.5f;
			float orthoTop = OrthographicSize * 0.5f;
			projection = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, OrthographicNear, OrthographicFar);
		}
		return projection;
	}

	std::vector<Entity> Entity::GetChildren() const
	{
		std::vector<Entity> children;
		for(UUID id : GetComponent<RelationshipComponent>().Children)
			if(Entity child = m_Scene->FindEntityByUUID(id))
				children.push_back(child);
		return children;
	}

	Scene::Scene() = default;

	Scene::~Scene()
	{
		if(m_Running)
			OnRuntimeStop();
	}

	namespace {

		template<typename... Components>
		void CopyComponents(ComponentGroup<Components...>, Registry& dst, EntityHandle dstEntity, Registry& src, EntityHandle srcEntity)
		{
			([&] {
				if(Components* component = src.TryGet<Components>(srcEntity))
					dst.AddOrReplace<Components>(dstEntity, *component);
			}(), ...);
		}

		void ResetRuntimeState(Registry& registry, EntityHandle e)
		{
			if(auto* rb = registry.TryGet<RigidBodyComponent>(e))
				rb->RuntimeBodyID = 0xFFFFFFFFu;
			if(auto* audio = registry.TryGet<AudioSourceComponent>(e))
				audio->RuntimeSoundID = 0xFFFFFFFFu;
			if(auto* script = registry.TryGet<ScriptComponent>(e))
				script->RuntimeInstanceRef = 0;
		}

	}

	Ref<Scene> Scene::Copy(const Scene& other)
	{
		Ref<Scene> result = CreateRef<Scene>();
		result->m_Settings = other.m_Settings;
		result->m_Name = other.m_Name;

		Scene& src = const_cast<Scene&>(other);
		src.m_Registry.EachEntity([&](EntityHandle srcEntity) {
			UUID uuid = src.m_Registry.Get<IDComponent>(srcEntity).ID;
			Entity dst = result->CreateEntityWithUUID(uuid, src.m_Registry.Get<TagComponent>(srcEntity).Tag);
			result->m_Registry.Get<RelationshipComponent>(dst) = src.m_Registry.Get<RelationshipComponent>(srcEntity);
			CopyComponents(AllComponents{}, result->m_Registry, dst, src.m_Registry, srcEntity);
			ResetRuntimeState(result->m_Registry, dst);
		});
		return result;
	}

	void Scene::RegisterUUID(UUID uuid, EntityHandle handle)
	{
		m_EntityMap[uuid] = handle;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		SF_CORE_ASSERT(!m_EntityMap.contains(uuid), "Duplicate entity UUID");
		EntityHandle handle = m_Registry.Create();
		m_Registry.Add<IDComponent>(handle, uuid);
		m_Registry.Add<TagComponent>(handle, name.empty() ? "Entity" : name);
		m_Registry.Add<RelationshipComponent>(handle);
		m_Registry.Add<TransformComponent>(handle);
		RegisterUUID(uuid, handle);
		return Entity(handle, this);
	}

	void Scene::DestroyEntityInternal(EntityHandle handle)
	{
		Entity entity(handle, this);
		if(!entity)
			return;
		for(auto& [id, callback] : m_DestroyCallbacks)
			callback(entity);
		m_EntityMap.erase(entity.GetUUID());
		m_Registry.Destroy(handle);
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if(!entity || entity.GetScene() != this)
			return;

		// Collect the subtree, children first.
		std::vector<EntityHandle> order;
		std::vector<Entity> stack{ entity };
		while(!stack.empty())
		{
			Entity current = stack.back();
			stack.pop_back();
			order.push_back(current.GetHandle());
			for(Entity child : current.GetChildren())
				stack.push_back(child);
		}

		// Detach from parent.
		if(Entity parent = entity.GetParent())
		{
			auto& children = parent.GetComponent<RelationshipComponent>().Children;
			std::erase(children, entity.GetUUID());
		}

		for(auto it = order.rbegin(); it != order.rend(); ++it)
			DestroyEntityInternal(*it);
	}

	void Scene::QueueDestroy(Entity entity)
	{
		if(entity && entity.GetScene() == this)
			m_DestroyQueue.push_back(entity.GetHandle());
	}

	void Scene::FlushDestroyQueue()
	{
		// Destroying a parent also destroys its children, so re-validate every handle.
		std::vector<EntityHandle> queue = std::move(m_DestroyQueue);
		m_DestroyQueue.clear();
		for(EntityHandle handle : queue)
			if(m_Registry.Valid(handle))
				DestroyEntity(Entity(handle, this));
	}

	uint32_t Scene::AddDestroyCallback(DestroyCallback callback)
	{
		uint32_t handle = m_NextCallbackHandle++;
		m_DestroyCallbacks.emplace_back(handle, std::move(callback));
		return handle;
	}

	void Scene::RemoveDestroyCallback(uint32_t handle)
	{
		std::erase_if(m_DestroyCallbacks, [handle](const auto& entry) { return entry.first == handle; });
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		if(!entity)
			return {};

		// Duplicates the subtree with fresh UUIDs; the copy of the root is attached to the same parent.
		std::unordered_map<UUID, UUID> remap;
		std::vector<Entity> sources;
		std::vector<Entity> stack{ entity };
		while(!stack.empty())
		{
			Entity current = stack.back();
			stack.pop_back();
			sources.push_back(current);
			remap[current.GetUUID()] = UUID();
			for(Entity child : current.GetChildren())
				stack.push_back(child);
		}

		Entity rootCopy;
		for(Entity source : sources)
		{
			Entity copy = CreateEntityWithUUID(remap[source.GetUUID()], source.GetName());
			CopyComponents(AllComponents{}, m_Registry, copy, m_Registry, source);
			ResetRuntimeState(m_Registry, copy);
			auto& rel = copy.GetComponent<RelationshipComponent>();
			const auto& srcRel = source.GetComponent<RelationshipComponent>();
			rel.Parent = source == entity ? UUID(0) : remap[srcRel.Parent];
			for(UUID child : srcRel.Children)
				rel.Children.push_back(remap[child]);
			if(source == entity)
				rootCopy = copy;
		}

		if(Entity parent = entity.GetParent())
			SetParent(rootCopy, parent, false);
		return rootCopy;
	}

	Entity Scene::FindEntityByUUID(UUID uuid)
	{
		auto it = m_EntityMap.find(uuid);
		if(it == m_EntityMap.end())
			return {};
		return Entity(it->second, this);
	}

	Entity Scene::FindEntityByName(const std::string& name)
	{
		Entity result;
		Each<TagComponent>([&](Entity entity, TagComponent& tag) {
			if(!result && tag.Tag == name)
				result = entity;
		});
		return result;
	}

	std::vector<Entity> Scene::GetEntities()
	{
		std::vector<Entity> entities;
		m_Registry.EachEntity([&](EntityHandle handle) { entities.emplace_back(handle, this); });
		return entities;
	}

	std::vector<Entity> Scene::GetRootEntities()
	{
		std::vector<Entity> roots;
		m_Registry.EachEntity([&](EntityHandle handle) {
			if(m_Registry.Get<RelationshipComponent>(handle).Parent.IsNull())
				roots.emplace_back(handle, this);
		});
		return roots;
	}

	bool Scene::IsDescendantOf(Entity entity, Entity potentialAncestor)
	{
		Entity current = entity.GetParent();
		while(current)
		{
			if(current == potentialAncestor)
				return true;
			current = current.GetParent();
		}
		return false;
	}

	void Scene::SetParent(Entity child, Entity parent, bool keepWorldTransform)
	{
		if(!child || child == parent)
			return;
		if(parent && (parent.GetScene() != this || IsDescendantOf(parent, child)))
		{
			SF_CORE_WARN("Cannot parent '{0}' to '{1}': would create a cycle", child.GetName(), parent.GetName());
			return;
		}

		glm::mat4 world = GetWorldTransform(child);

		auto& rel = child.GetComponent<RelationshipComponent>();
		if(Entity oldParent = child.GetParent())
			std::erase(oldParent.GetComponent<RelationshipComponent>().Children, child.GetUUID());

		if(parent)
		{
			rel.Parent = parent.GetUUID();
			parent.GetComponent<RelationshipComponent>().Children.push_back(child.GetUUID());
		}
		else
		{
			rel.Parent = UUID(0);
		}

		if(keepWorldTransform)
			SetWorldTransform(child, world);
	}

	glm::mat4 Scene::GetWorldTransform(Entity entity)
	{
		glm::mat4 world = entity.GetComponent<TransformComponent>().GetTransform();
		Entity parent = entity.GetParent();
		int guard = 0;
		while(parent && guard++ < 1024)
		{
			world = parent.GetComponent<TransformComponent>().GetTransform() * world;
			parent = parent.GetParent();
		}
		return world;
	}

	void Scene::SetWorldTransform(Entity entity, const glm::mat4& world)
	{
		glm::mat4 local = world;
		if(Entity parent = entity.GetParent())
			local = glm::inverse(GetWorldTransform(parent)) * world;
		auto& tc = entity.GetComponent<TransformComponent>();
		Math::DecomposeTransform(local, tc.Translation, tc.Rotation, tc.Scale);
	}

	Entity Scene::InstantiatePrefab(const AssetPath& path)
	{
		return InstantiatePrefab(path, nullptr, Entity());
	}

	Entity Scene::InstantiatePrefab(const AssetPath& path, const glm::vec3* position, Entity parent)
	{
		Entity root = SceneSerializer::DeserializePrefab(*this, path);
		if(!root)
			return {};
		if(position)
			root.Transform().Translation = *position;
		if(parent)
			SetParent(root, parent, false);
		return root;
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		Entity result;
		Each<CameraComponent>([&](Entity entity, CameraComponent& camera) {
			if(camera.Primary && !result)
				result = entity;
		});
		return result;
	}

	void Scene::OnRuntimeStart()
	{
		if(m_Running)
			return;
		m_Running = true;
		m_Paused = false;
		m_FrameCount = 0;
		m_Time = 0.0f;
		m_PhysicsWorld = CreateScope<PhysicsWorld>(this);
		m_AudioWorld = CreateScope<AudioWorld>(this);
		m_ScriptWorld = CreateScope<ScriptWorld>(this);
		m_PhysicsWorld->OnStart();
		m_AudioWorld->OnStart();
		m_ScriptWorld->OnStart();
	}

	void Scene::OnRuntimeStop()
	{
		if(!m_Running)
			return;
		m_ScriptWorld->OnStop();
		m_AudioWorld->OnStop();
		m_PhysicsWorld->OnStop();
		m_ScriptWorld.reset();
		m_AudioWorld.reset();
		m_PhysicsWorld.reset();
		m_DestroyQueue.clear();
		m_Running = false;
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if(!m_Running)
			return;

		bool step = m_StepFrames > 0;
		if(m_Paused && !step)
			return;
		if(step)
			m_StepFrames--;

		m_FrameCount++;
		m_Time += ts;

		m_ScriptWorld->OnUpdate(ts);
		FlushDestroyQueue();
		m_PhysicsWorld->OnUpdate(ts);
		m_ScriptWorld->OnLateUpdate(ts);
		m_AudioWorld->OnUpdate(ts);
		FlushDestroyQueue();
	}

}
