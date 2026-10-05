#include "Starfall/Scripting/ScriptWorld.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Physics/PhysicsWorld.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Scripting/ScriptInternal.h"

#include <algorithm>

namespace Starfall {

	namespace {

		// Runaway-script protection: every Lua entry point resets the budget; the hook aborts the script when exhausted.
		constexpr int HookInterval = 100000;      // VM instructions between hook calls
		constexpr int MaxHooksPerCall = 500;      // ~50M instructions per script entry point
		int s_HooksRemaining = MaxHooksPerCall;

		void BudgetHook(lua_State* L, lua_Debug*)
		{
			if(--s_HooksRemaining <= 0)
			{
				s_HooksRemaining = MaxHooksPerCall;
				luaL_error(L, "script exceeded its execution budget (possible infinite loop)");
			}
		}

		void ResetBudget() { s_HooksRemaining = MaxHooksPerCall; }

		std::function<void()> s_QuitHandler;

		std::string ErrorMessage(const sol::protected_function_result& result)
		{
			sol::error err = result;
			return err.what();
		}

		template<typename... Args>
		bool SafeCall(ScriptWorld::Impl& impl, ScriptInstance& instance, sol::protected_function& fn, const std::string& what, Args&&... args)
		{
			if(!fn.valid())
				return true;
			ResetBudget();
			sol::protected_function_result result = fn(instance.Self, std::forward<Args>(args)...);
			if(!result.valid())
			{
				instance.Faulted = true;
				impl.ReportError(instance.Script + " (" + what + ")", ErrorMessage(result));
				return false;
			}
			return true;
		}

		ScriptEntity MakeScriptEntity(Scene* scene, Entity entity) { return { scene, static_cast<uint64_t>(entity.GetUUID()) }; }

	}

	void ScriptWorld::Impl::ReportError(const std::string& where, const std::string& message)
	{
		std::string full = where + ": " + message;
		SF_CORE_ERROR("Script error: {0}", full);
		Errors.push_back(std::move(full));
		if(Errors.size() > 256)
			Errors.erase(Errors.begin());
	}

	bool ScriptWorld::Impl::LoadClass(const AssetPath& path, sol::table& outClass)
	{
		if(auto it = Classes.find(path); it != Classes.end())
		{
			outClass = it->second;
			return true;
		}

		std::filesystem::path file = Project::ResolvePath(path);
		auto text = file.empty() ? std::nullopt : FileSystem::ReadText(file);
		if(!text)
		{
			ReportError(path, "script file not found");
			return false;
		}

		ResetBudget();
		sol::load_result chunk = Lua.load(*text, "@" + path, sol::load_mode::text);
		if(!chunk.valid())
		{
			sol::error err = chunk;
			ReportError(path, err.what());
			return false;
		}
		sol::protected_function_result result = chunk();
		if(!result.valid())
		{
			ReportError(path, ErrorMessage(result));
			return false;
		}
		sol::object value = result;
		if(value.get_type() != sol::type::table)
		{
			ReportError(path, "script must return a class table");
			return false;
		}
		outClass = value.as<sol::table>();
		Classes[path] = outClass;
		return true;
	}

	bool ScriptWorld::Impl::CreateInstance(Entity entity)
	{
		auto* component = entity.TryGetComponent<ScriptComponent>();
		if(!component || component->Script.empty() || Instances.contains(entity.GetUUID()))
			return false;

		if(auto failed = FailedLoads.find(entity.GetUUID()); failed != FailedLoads.end())
		{
			if(failed->second == component->Script)
				return false;
			FailedLoads.erase(failed);
		}

		sol::table klass;
		if(!LoadClass(component->Script, klass))
		{
			FailedLoads[entity.GetUUID()] = component->Script;
			return false;
		}

		ScriptInstance instance;
		instance.Script = component->Script;
		instance.Self = Lua.create_table();
		sol::table meta = Lua.create_table();
		meta["__index"] = klass;
		instance.Self[sol::metatable_key] = meta;

		// Exported properties: class defaults, then per-entity overrides.
		if(sol::optional<sol::table> defaults = klass["properties"])
			for(const auto& [key, value] : *defaults)
				instance.Self[key] = value;
		for(const auto& [key, value] : component->Properties)
			std::visit([&](const auto& v) { instance.Self[key] = v; }, value);
		instance.Self["entity"] = MakeScriptEntity(SceneRef, entity);

		auto bind = [&](const char* name, sol::protected_function& slot) {
			sol::object fn = klass[name];
			if(fn.get_type() == sol::type::function)
				slot = fn.as<sol::protected_function>();
		};
		bind("OnUpdate", instance.OnUpdate);
		bind("OnLateUpdate", instance.OnLateUpdate);
		bind("OnDestroy", instance.OnDestroy);
		bind("OnCollisionEnter", instance.OnCollisionEnter);
		bind("OnCollisionExit", instance.OnCollisionExit);
		bind("OnTriggerEnter", instance.OnTriggerEnter);
		bind("OnTriggerExit", instance.OnTriggerExit);

		sol::table self = instance.Self;
		Instances.emplace(entity.GetUUID(), std::move(instance));
		component->RuntimeInstanceRef = 1;
		CallOnCreate(self, component->Script, entity);
		return true;
	}

	void ScriptWorld::Impl::CallOnCreate(const sol::table& self, const std::string& scriptPath, Entity entity)
	{
		sol::object fn = self["OnCreate"];
		if(fn.get_type() != sol::type::function)
			return;
		ResetBudget();
		sol::protected_function_result result = fn.as<sol::protected_function>()(self);
		if(!result.valid())
		{
			ReportError(scriptPath + " (OnCreate, entity '" + entity.GetName() + "')", ErrorMessage(result));
			if(auto it = Instances.find(entity.GetUUID()); it != Instances.end())
				it->second.Faulted = true;
		}
	}

	void ScriptWorld::Impl::DestroyInstance(UUID id, bool callOnDestroy)
	{
		auto it = Instances.find(id);
		if(it == Instances.end())
			return;
		// Move out first: OnDestroy may spawn/destroy entities and mutate the map.
		ScriptInstance instance = std::move(it->second);
		Instances.erase(it);
		if(callOnDestroy && !instance.Faulted)
			SafeCall(*this, instance, instance.OnDestroy, "OnDestroy");
	}

	void ScriptWorld::Impl::DispatchPhysicsEvents()
	{
		PhysicsWorld* physics = SceneRef->GetPhysicsWorld();
		if(!physics)
			return;

		auto dispatch = [&](UUID self, UUID other, sol::protected_function ScriptInstance::* slot, const char* name) {
			auto it = Instances.find(self);
			if(it == Instances.end() || it->second.Faulted)
				return;
			Entity otherEntity = SceneRef->FindEntityByUUID(other);
			if(!otherEntity)
				return;
			SafeCall(*this, it->second, it->second.*slot, name, MakeScriptEntity(SceneRef, otherEntity));
		};

		for(const CollisionEvent& e : physics->GetEvents())
		{
			switch(e.Type)
			{
				case CollisionEventType::CollisionBegin:
					dispatch(e.EntityA, e.EntityB, &ScriptInstance::OnCollisionEnter, "OnCollisionEnter");
					dispatch(e.EntityB, e.EntityA, &ScriptInstance::OnCollisionEnter, "OnCollisionEnter");
					break;
				case CollisionEventType::CollisionEnd:
					dispatch(e.EntityA, e.EntityB, &ScriptInstance::OnCollisionExit, "OnCollisionExit");
					dispatch(e.EntityB, e.EntityA, &ScriptInstance::OnCollisionExit, "OnCollisionExit");
					break;
				case CollisionEventType::TriggerBegin:
					dispatch(e.EntityA, e.EntityB, &ScriptInstance::OnTriggerEnter, "OnTriggerEnter");
					dispatch(e.EntityB, e.EntityA, &ScriptInstance::OnTriggerEnter, "OnTriggerEnter");
					break;
				case CollisionEventType::TriggerEnd:
					dispatch(e.EntityA, e.EntityB, &ScriptInstance::OnTriggerExit, "OnTriggerExit");
					dispatch(e.EntityB, e.EntityA, &ScriptInstance::OnTriggerExit, "OnTriggerExit");
					break;
			}
		}
	}

	void ScriptInstantiateSubtree(ScriptWorld::Impl& impl, Entity root)
	{
		std::vector<Entity> stack{ root };
		std::vector<Entity> withScripts;
		while(!stack.empty())
		{
			Entity current = stack.back();
			stack.pop_back();
			if(current.HasComponent<ScriptComponent>())
				withScripts.push_back(current);
			for(Entity child : current.GetChildren())
				stack.push_back(child);
		}
		for(Entity entity : withScripts)
			impl.CreateInstance(entity);
	}

	ScriptWorld::ScriptWorld(Scene* scene)
		: m_Impl(CreateScope<Impl>(scene)), m_Scene(scene)
	{
		sol::state& lua = m_Impl->Lua;
		lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8, sol::lib::coroutine);

		// Sandbox: no raw file loading, no os/io/debug/package. Scripts load siblings through require().
		lua["dofile"] = sol::nil;
		lua["loadfile"] = sol::nil;
		lua["collectgarbage"] = sol::nil;
		sol::table os = lua.create_named_table("os");
		os["time"] = []() { return static_cast<double>(std::time(nullptr)); };
		os["clock"] = []() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; };

		Impl* impl = m_Impl.get();
		sol::table loaded = lua.create_table();
		lua["require"] = [impl, loaded](const std::string& name, sol::this_state ts) mutable -> sol::object {
			sol::state_view view(ts);
			if(sol::object cached = loaded[name]; cached.valid() && cached.get_type() != sol::type::lua_nil)
				return cached;
			std::string path = "Scripts/" + name;
			std::replace(path.begin(), path.end(), '.', '/');
			path += ".lua";
			auto text = Project::IsValidAssetPath(path) ? FileSystem::ReadText(Project::ResolvePath(path)) : std::nullopt;
			if(!text)
				throw std::runtime_error("module '" + name + "' not found");
			sol::load_result chunk = view.load(*text, "@" + path, sol::load_mode::text);
			if(!chunk.valid())
			{
				sol::error err = chunk;
				throw std::runtime_error(err.what());
			}
			sol::protected_function_result result = chunk();
			if(!result.valid())
			{
				sol::error err = result;
				throw std::runtime_error(err.what());
			}
			sol::object value = result;
			loaded[name] = value;
			return value;
		};

		lua_sethook(lua.lua_state(), BudgetHook, LUA_MASKCOUNT, HookInterval);
		RegisterScriptAPI(*m_Impl);
	}

	ScriptWorld::~ScriptWorld()
	{
		OnStop();
	}

	void ScriptWorld::OnStart()
	{
		m_Impl->Started = true;
		m_DestroyCallback = m_Scene->AddDestroyCallback([impl = m_Impl.get()](Entity entity) { impl->DestroyInstance(entity.GetUUID(), true); });

		std::vector<Entity> entities;
		m_Scene->Each<ScriptComponent>([&](Entity entity, ScriptComponent&) { entities.push_back(entity); });
		for(Entity entity : entities)
			m_Impl->CreateInstance(entity);
	}

	void ScriptWorld::OnStop()
	{
		if(!m_Impl || !m_Impl->Started)
			return;
		m_Impl->Started = false;
		m_Scene->RemoveDestroyCallback(m_DestroyCallback);
		m_DestroyCallback = 0;
		std::vector<UUID> ids;
		for(auto& [id, instance] : m_Impl->Instances)
			ids.push_back(id);
		for(UUID id : ids)
			m_Impl->DestroyInstance(id, true);
		m_Impl->Instances.clear();
		m_Impl->Classes.clear();
	}

	void ScriptWorld::OnUpdate(float deltaTime)
	{
		sol::state& lua = m_Impl->Lua;
		lua["Time"]["delta"] = deltaTime;
		lua["Time"]["time"] = m_Scene->GetTime();
		lua["Time"]["frame"] = static_cast<double>(m_Scene->GetFrameCount());

		// Pick up entities spawned since the last frame (C++ side spawns, prefab instantiation by other systems).
		std::vector<Entity> pending;
		m_Scene->Each<ScriptComponent>([&](Entity entity, ScriptComponent& script) {
			if(!script.Script.empty() && !m_Impl->Instances.contains(entity.GetUUID()))
				pending.push_back(entity);
		});
		for(Entity entity : pending)
			m_Impl->CreateInstance(entity);

		// Snapshot ids: scripts may create/destroy instances while we iterate.
		std::vector<UUID> ids;
		ids.reserve(m_Impl->Instances.size());
		for(auto& [id, instance] : m_Impl->Instances)
			ids.push_back(id);
		for(UUID id : ids)
		{
			auto it = m_Impl->Instances.find(id);
			if(it == m_Impl->Instances.end() || it->second.Faulted)
				continue;
			ScriptInstance& instance = it->second;
			SafeCall(*m_Impl, instance, instance.OnUpdate, "OnUpdate", deltaTime);
		}
	}

	void ScriptWorld::OnLateUpdate(float deltaTime)
	{
		m_Impl->DispatchPhysicsEvents();

		std::vector<UUID> ids;
		for(auto& [id, instance] : m_Impl->Instances)
			ids.push_back(id);
		for(UUID id : ids)
		{
			auto it = m_Impl->Instances.find(id);
			if(it == m_Impl->Instances.end() || it->second.Faulted)
				continue;
			SafeCall(*m_Impl, it->second, it->second.OnLateUpdate, "OnLateUpdate", deltaTime);
		}
	}

	bool ScriptWorld::ExecuteString(const std::string& code, std::string* error)
	{
		ResetBudget();
		sol::protected_function_result result = m_Impl->Lua.safe_script(code, sol::script_pass_on_error, "=console");
		if(!result.valid())
		{
			std::string message = ErrorMessage(result);
			m_Impl->ReportError("console", message);
			if(error)
				*error = message;
			return false;
		}
		return true;
	}

	double ScriptWorld::GetGlobalNumber(const std::string& name, double fallback) const
	{
		sol::optional<double> value = m_Impl->Lua[name];
		return value.value_or(fallback);
	}

	std::string ScriptWorld::GetGlobalString(const std::string& name, const std::string& fallback) const
	{
		sol::optional<std::string> value = m_Impl->Lua[name];
		return value.value_or(fallback);
	}

	void ScriptWorld::InstantiateScripts(Entity root)
	{
		if(root)
			ScriptInstantiateSubtree(*m_Impl, root);
	}

	size_t ScriptWorld::GetInstanceCount() const { return m_Impl->Instances.size(); }
	const std::vector<std::string>& ScriptWorld::GetErrors() const { return m_Impl->Errors; }
	void ScriptWorld::ClearErrors() { m_Impl->Errors.clear(); }
	void ScriptWorld::SetQuitHandler(std::function<void()> handler) { s_QuitHandler = std::move(handler); }

	// Used by the API bindings.
	void ScriptRequestQuit()
	{
		if(s_QuitHandler)
			s_QuitHandler();
	}

}
