#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Core/Timestep.h"
#include "Starfall/ECS/Registry.h"
#include "Starfall/Scene/Components.h"
#include "Starfall/Scene/SceneSettings.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Starfall {

	class Entity;
	class PhysicsWorld;
	class AudioWorld;
	class ScriptWorld;

	class Scene
	{
	public:
		Scene();
		~Scene();
		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Deep copy of all entities/components/settings (runtime state is not copied). Used for play mode.
		static Ref<Scene> Copy(const Scene& other);

		Entity CreateEntity(const std::string& name = "Entity");
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
		// Destroys the entity and all its descendants immediately. Do not call while iterating; use QueueDestroy from scripts/systems.
		void DestroyEntity(Entity entity);
		// Destroys at the end of the current update (safe during iteration and from scripts).
		void QueueDestroy(Entity entity);
		Entity DuplicateEntity(Entity entity);

		Entity FindEntityByUUID(UUID uuid);
		Entity FindEntityByName(const std::string& name);
		std::vector<Entity> GetEntities();
		std::vector<Entity> GetRootEntities();
		size_t GetEntityCount() const { return m_Registry.AliveCount(); }

		// Defined in Entity.h (needs the complete Entity type). Callback: (Entity, Components&...).
		template<typename... Components, typename Fn>
		void Each(Fn&& fn);

		// Hierarchy
		void SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
		bool IsDescendantOf(Entity entity, Entity potentialAncestor);
		glm::mat4 GetWorldTransform(Entity entity);
		void SetWorldTransform(Entity entity, const glm::mat4& world);

		// Prefabs: instantiates a serialized entity subtree (path relative to the project asset directory).
		Entity InstantiatePrefab(const AssetPath& path);
		Entity InstantiatePrefab(const AssetPath& path, const glm::vec3* position, Entity parent);

		// Runtime
		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdateRuntime(Timestep ts);
		bool IsRunning() const { return m_Running; }
		void SetPaused(bool paused) { m_Paused = paused; }
		bool IsPaused() const { return m_Paused; }
		void Step(uint32_t frames = 1) { m_StepFrames = frames; }

		Entity GetPrimaryCameraEntity();

		SceneSettings& GetSettings() { return m_Settings; }
		const SceneSettings& GetSettings() const { return m_Settings; }
		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		PhysicsWorld* GetPhysicsWorld() { return m_PhysicsWorld.get(); }
		AudioWorld* GetAudioWorld() { return m_AudioWorld.get(); }
		ScriptWorld* GetScriptWorld() { return m_ScriptWorld.get(); }

		uint64_t GetFrameCount() const { return m_FrameCount; }
		float GetTime() const { return m_Time; }

		// Invoked when an entity is about to be destroyed (physics/audio/script cleanup, editor selection).
		using DestroyCallback = std::function<void(Entity)>;
		uint32_t AddDestroyCallback(DestroyCallback callback);
		void RemoveDestroyCallback(uint32_t handle);

	private:
		friend class Entity;
		friend class SceneSerializer;

		void FlushDestroyQueue();
		void DestroyEntityInternal(EntityHandle handle);
		void RegisterUUID(UUID uuid, EntityHandle handle);

		Registry m_Registry;
		std::unordered_map<UUID, EntityHandle> m_EntityMap;
		std::vector<EntityHandle> m_DestroyQueue;
		std::vector<std::pair<uint32_t, DestroyCallback>> m_DestroyCallbacks;
		uint32_t m_NextCallbackHandle = 1;

		SceneSettings m_Settings;
		std::string m_Name = "Untitled";
		bool m_Running = false;
		bool m_Paused = false;
		uint32_t m_StepFrames = 0;
		uint64_t m_FrameCount = 0;
		float m_Time = 0.0f;

		Scope<PhysicsWorld> m_PhysicsWorld;
		Scope<AudioWorld> m_AudioWorld;
		Scope<ScriptWorld> m_ScriptWorld;
	};

}
