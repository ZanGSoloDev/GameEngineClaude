#pragma once

// Private to the scripting module (pulls in sol2, which is heavy to compile).

#include "Starfall/Scene/Entity.h"
#include "Starfall/Scripting/ScriptWorld.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Starfall {

	struct ScriptInstance
	{
		sol::table Self;
		sol::protected_function OnUpdate;
		sol::protected_function OnLateUpdate;
		sol::protected_function OnDestroy;
		sol::protected_function OnCollisionEnter;
		sol::protected_function OnCollisionExit;
		sol::protected_function OnTriggerEnter;
		sol::protected_function OnTriggerExit;
		AssetPath Script;
		bool Faulted = false;
	};

	struct ScriptWorld::Impl
	{
		explicit Impl(Scene* scene) : SceneRef(scene) {}

		Scene* SceneRef;
		sol::state Lua;
		std::unordered_map<UUID, ScriptInstance> Instances;
		std::unordered_map<AssetPath, sol::table> Classes;
		std::vector<std::string> Errors;
		bool Started = false;
		// Entities whose script failed to load, with the path that failed; retried only if the path changes.
		std::unordered_map<UUID, AssetPath> FailedLoads;

		void ReportError(const std::string& where, const std::string& message);
		bool LoadClass(const AssetPath& path, sol::table& outClass);
		bool CreateInstance(Entity entity);
		void CallOnCreate(const sol::table& self, const std::string& scriptPath, Entity entity);
		void DestroyInstance(UUID id, bool callOnDestroy);
		void DispatchPhysicsEvents();
	};

	// Handle given to Lua for an entity: resolved against the scene on every access so stale handles are safe.
	struct ScriptEntity
	{
		Scene* SceneRef = nullptr;
		uint64_t ID = 0;

		Entity Resolve() const { return SceneRef ? SceneRef->FindEntityByUUID(UUID(ID)) : Entity(); }
	};

	// Registers the full Lua API (Vec3, Entity, components, Scene, Input, Audio, Log, Time, Mathf, Application).
	void RegisterScriptAPI(ScriptWorld::Impl& impl);

	// Called by the API when a script creates entities or adds a Script component at runtime.
	void ScriptInstantiateSubtree(ScriptWorld::Impl& impl, Entity root);

	// Application.Quit() support.
	void ScriptRequestQuit();

}
