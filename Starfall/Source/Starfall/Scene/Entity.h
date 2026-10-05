#pragma once

#include "Starfall/Scene/Scene.h"

namespace Starfall {

	// Lightweight handle (Scene*, entity). Cheap to copy; validity is checked via operator bool.
	class Entity
	{
	public:
		Entity() = default;
		Entity(EntityHandle handle, Scene* scene) : m_Handle(handle), m_Scene(scene) {}

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			SF_CORE_ASSERT(!HasComponent<T>(), "Entity already has component");
			return m_Scene->m_Registry.Add<T>(m_Handle, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			return m_Scene->m_Registry.AddOrReplace<T>(m_Handle, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent() const
		{
			SF_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			return m_Scene->m_Registry.Get<T>(m_Handle);
		}

		template<typename T>
		T* TryGetComponent() const { return m_Scene ? m_Scene->m_Registry.TryGet<T>(m_Handle) : nullptr; }

		template<typename... T>
		bool HasComponent() const { return m_Scene && m_Scene->m_Registry.Has<T...>(m_Handle); }

		template<typename T>
		void RemoveComponent()
		{
			SF_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			m_Scene->m_Registry.Remove<T>(m_Handle);
		}

		bool IsValid() const { return m_Scene && m_Scene->m_Registry.Valid(m_Handle); }
		explicit operator bool() const { return IsValid(); }
		operator EntityHandle() const { return m_Handle; }
		operator uint32_t() const { return static_cast<uint32_t>(m_Handle); }

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		const std::string& GetName() const { return GetComponent<TagComponent>().Tag; }
		TransformComponent& Transform() const { return GetComponent<TransformComponent>(); }

		Entity GetParent() const { return m_Scene->FindEntityByUUID(GetComponent<RelationshipComponent>().Parent); }
		void SetParent(Entity parent, bool keepWorldTransform = true) { m_Scene->SetParent(*this, parent, keepWorldTransform); }
		std::vector<Entity> GetChildren() const;

		Scene* GetScene() const { return m_Scene; }
		EntityHandle GetHandle() const { return m_Handle; }

		bool operator==(const Entity& other) const { return m_Handle == other.m_Handle && m_Scene == other.m_Scene; }
		bool operator!=(const Entity& other) const { return !(*this == other); }

	private:
		EntityHandle m_Handle = EntityHandle::Null;
		Scene* m_Scene = nullptr;
	};

	template<typename... Components, typename Fn>
	void Scene::Each(Fn&& fn)
	{
		m_Registry.Each<Components...>([&](EntityHandle handle, Components&... components) {
			fn(Entity(handle, this), components...);
		});
	}

}
