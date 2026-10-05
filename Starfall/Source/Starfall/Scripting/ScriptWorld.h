#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Core/UUID.h"

#include <functional>
#include <string>
#include <vector>

namespace Starfall {

	class Scene;
	class Entity;

	// Lua scripting host for one running Scene. Created by Scene::OnRuntimeStart.
	//
	// A script file returns a class table:
	//   local M = {}
	//   M.properties = { speed = 1.0 }          -- optional: exported defaults (overridable per entity)
	//   function M:OnCreate() end               -- self.entity is the owning entity
	//   function M:OnUpdate(dt) end
	//   function M:OnLateUpdate(dt) end
	//   function M:OnDestroy() end
	//   function M:OnCollisionEnter(other) end  -- also OnCollisionExit, OnTriggerEnter, OnTriggerExit
	//   return M
	class ScriptWorld
	{
	public:
		explicit ScriptWorld(Scene* scene);
		~ScriptWorld();
		ScriptWorld(const ScriptWorld&) = delete;
		ScriptWorld& operator=(const ScriptWorld&) = delete;

		void OnStart();
		void OnStop();
		void OnUpdate(float deltaTime);
		void OnLateUpdate(float deltaTime);

		// Runs a Lua snippet in the scene's script state (console, tests). Returns false and fills `error` on failure.
		bool ExecuteString(const std::string& code, std::string* error = nullptr);
		// Reads a global number set by a script (tests / tools). Returns fallback if missing or not a number.
		double GetGlobalNumber(const std::string& name, double fallback = 0.0) const;
		std::string GetGlobalString(const std::string& name, const std::string& fallback = {}) const;

		// Creates script instances for entities (and descendants) that have a ScriptComponent but no instance yet.
		void InstantiateScripts(Entity root);

		size_t GetInstanceCount() const;
		// Errors raised by scripts since start (script path + message), newest last.
		const std::vector<std::string>& GetErrors() const;
		void ClearErrors();

		// Invoked by Application.Quit() from scripts.
		static void SetQuitHandler(std::function<void()> handler);

		// Implementation detail; public only so the scripting module's own translation units can name it.
		struct Impl;

	private:
		Scope<Impl> m_Impl;
		Scene* m_Scene;
		uint32_t m_DestroyCallback = 0;
	};

}
