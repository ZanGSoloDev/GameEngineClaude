#pragma once

#include "Starfall/Core/Base.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Starfall {

	// Entity handle: 20-bit index, 12-bit generation. Null is all ones.
	enum class EntityHandle : uint32_t { Null = 0xFFFFFFFFu };

	namespace ECSDetail {

		constexpr uint32_t IndexBits = 20;
		constexpr uint32_t IndexMask = (1u << IndexBits) - 1;
		constexpr uint32_t GenerationMask = (1u << (32 - IndexBits)) - 1;

		constexpr uint32_t Index(EntityHandle e) { return static_cast<uint32_t>(e) & IndexMask; }
		constexpr uint32_t Generation(EntityHandle e) { return static_cast<uint32_t>(e) >> IndexBits; }
		constexpr EntityHandle Make(uint32_t index, uint32_t generation) { return static_cast<EntityHandle>((generation << IndexBits) | index); }

		class IPool
		{
		public:
			virtual ~IPool() = default;
			virtual void Remove(EntityHandle e) = 0;
			virtual bool Contains(EntityHandle e) const = 0;
			virtual size_t Size() const = 0;
			virtual const std::vector<EntityHandle>& Entities() const = 0;
		};

		// Sparse set keyed by entity index; dense arrays keep components contiguous.
		template<typename T>
		class Pool final : public IPool
		{
		public:
			template<typename... Args>
			T& Emplace(EntityHandle e, Args&&... args)
			{
				uint32_t index = Index(e);
				if(index >= m_Sparse.size())
					m_Sparse.resize(static_cast<size_t>(index) + 1, Invalid);
				SF_CORE_ASSERT(m_Sparse[index] == Invalid, "Component already present");
				m_Sparse[index] = static_cast<uint32_t>(m_Dense.size());
				m_Dense.push_back(e);
				m_Components.emplace_back(std::forward<Args>(args)...);
				return m_Components.back();
			}

			T* TryGet(EntityHandle e)
			{
				uint32_t index = Index(e);
				if(index >= m_Sparse.size() || m_Sparse[index] == Invalid)
					return nullptr;
				return &m_Components[m_Sparse[index]];
			}

			void Remove(EntityHandle e) override
			{
				uint32_t index = Index(e);
				if(index >= m_Sparse.size() || m_Sparse[index] == Invalid)
					return;
				uint32_t slot = m_Sparse[index];
				uint32_t last = static_cast<uint32_t>(m_Dense.size()) - 1;
				if(slot != last)
				{
					m_Dense[slot] = m_Dense[last];
					m_Components[slot] = std::move(m_Components[last]);
					m_Sparse[Index(m_Dense[slot])] = slot;
				}
				m_Dense.pop_back();
				m_Components.pop_back();
				m_Sparse[index] = Invalid;
			}

			bool Contains(EntityHandle e) const override
			{
				uint32_t index = Index(e);
				return index < m_Sparse.size() && m_Sparse[index] != Invalid;
			}

			size_t Size() const override { return m_Dense.size(); }
			const std::vector<EntityHandle>& Entities() const override { return m_Dense; }

		private:
			static constexpr uint32_t Invalid = 0xFFFFFFFFu;
			std::vector<uint32_t> m_Sparse;
			std::vector<EntityHandle> m_Dense;
			std::vector<T> m_Components;
		};

	}

	// Minimal sparse-set ECS registry. Single threaded.
	class Registry
	{
	public:
		Registry() = default;
		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;

		EntityHandle Create()
		{
			uint32_t index;
			if(!m_FreeList.empty())
			{
				index = m_FreeList.back();
				m_FreeList.pop_back();
			}
			else
			{
				index = static_cast<uint32_t>(m_Generations.size());
				SF_CORE_ASSERT(index <= ECSDetail::IndexMask, "Entity limit reached");
				m_Generations.push_back(0);
				m_Live.push_back(false);
			}
			m_Live[index] = true;
			m_AliveCount++;
			return ECSDetail::Make(index, m_Generations[index]);
		}

		bool Valid(EntityHandle e) const
		{
			if(e == EntityHandle::Null)
				return false;
			uint32_t index = ECSDetail::Index(e);
			return index < m_Generations.size() && m_Live[index] && m_Generations[index] == ECSDetail::Generation(e);
		}

		void Destroy(EntityHandle e)
		{
			if(!Valid(e))
				return;
			for(auto& [type, pool] : m_Pools)
				pool->Remove(e);
			uint32_t index = ECSDetail::Index(e);
			m_Generations[index] = (m_Generations[index] + 1) & ECSDetail::GenerationMask;
			m_Live[index] = false;
			m_FreeList.push_back(index);
			m_AliveCount--;
		}

		size_t AliveCount() const { return m_AliveCount; }

		template<typename T, typename... Args>
		T& Add(EntityHandle e, Args&&... args)
		{
			SF_CORE_ASSERT(Valid(e), "Invalid entity");
			return GetOrCreatePool<T>().Emplace(e, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		T& AddOrReplace(EntityHandle e, Args&&... args)
		{
			if(T* existing = TryGet<T>(e))
			{
				*existing = T(std::forward<Args>(args)...);
				return *existing;
			}
			return Add<T>(e, std::forward<Args>(args)...);
		}

		template<typename T>
		T& Get(EntityHandle e)
		{
			T* component = TryGet<T>(e);
			SF_CORE_ASSERT(component, "Entity does not have component");
			return *component;
		}

		template<typename T>
		const T& Get(EntityHandle e) const { return const_cast<Registry*>(this)->Get<T>(e); }

		template<typename T>
		T* TryGet(EntityHandle e)
		{
			if(!Valid(e))
				return nullptr;
			auto* pool = FindPool<T>();
			return pool ? pool->TryGet(e) : nullptr;
		}

		template<typename T>
		const T* TryGet(EntityHandle e) const { return const_cast<Registry*>(this)->TryGet<T>(e); }

		template<typename... Ts>
		bool Has(EntityHandle e) const { return Valid(e) && (HasOne<Ts>(e) && ...); }

		template<typename T>
		void Remove(EntityHandle e)
		{
			if(auto* pool = FindPool<T>())
				pool->Remove(e);
		}

		// Iterates entities having all Ts. Callback: (EntityHandle, Ts&...).
		// The entity list is snapshotted, so the callback may destroy entities or change components.
		template<typename... Ts, typename Fn>
		void Each(Fn&& fn)
		{
			static_assert(sizeof...(Ts) > 0);
			if(!((FindPool<Ts>() != nullptr) && ...))
				return;
			const ECSDetail::IPool* smallest = nullptr;
			((smallest = (!smallest || FindPool<Ts>()->Size() < smallest->Size()) ? FindPool<Ts>() : smallest), ...);
			std::vector<EntityHandle> snapshot = smallest->Entities();
			for(EntityHandle e : snapshot)
			{
				if(!Valid(e) || !(HasOne<Ts>(e) && ...))
					continue;
				fn(e, *FindPool<Ts>()->TryGet(e)...);
			}
		}

		// Iterates every live entity (snapshotted).
		template<typename Fn>
		void EachEntity(Fn&& fn)
		{
			std::vector<EntityHandle> snapshot;
			snapshot.reserve(m_AliveCount);
			for(uint32_t i = 0; i < m_Generations.size(); i++)
				if(m_Live[i])
					snapshot.push_back(ECSDetail::Make(i, m_Generations[i]));
			for(EntityHandle e : snapshot)
				if(Valid(e))
					fn(e);
		}

		void Clear()
		{
			m_Pools.clear();
			m_Generations.clear();
			m_Live.clear();
			m_FreeList.clear();
			m_AliveCount = 0;
		}

	private:
		template<typename T>
		bool HasOne(EntityHandle e) const
		{
			auto* pool = FindPool<T>();
			return pool && pool->Contains(e);
		}

		template<typename T>
		ECSDetail::Pool<T>* FindPool() const
		{
			auto it = m_Pools.find(std::type_index(typeid(T)));
			return it == m_Pools.end() ? nullptr : static_cast<ECSDetail::Pool<T>*>(it->second.get());
		}

		template<typename T>
		ECSDetail::Pool<T>& GetOrCreatePool()
		{
			auto key = std::type_index(typeid(T));
			auto it = m_Pools.find(key);
			if(it == m_Pools.end())
				it = m_Pools.emplace(key, std::make_unique<ECSDetail::Pool<T>>()).first;
			return *static_cast<ECSDetail::Pool<T>*>(it->second.get());
		}

		std::unordered_map<std::type_index, std::unique_ptr<ECSDetail::IPool>> m_Pools;
		std::vector<uint32_t> m_Generations;
		std::vector<bool> m_Live;
		std::vector<uint32_t> m_FreeList;
		size_t m_AliveCount = 0;
	};

}
