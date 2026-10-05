#pragma once

#include <cstdint>
#include <functional>

namespace Starfall {

	class UUID
	{
	public:
		UUID();
		explicit constexpr UUID(uint64_t uuid) : m_UUID(uuid) {}
		UUID(const UUID&) = default;

		constexpr operator uint64_t() const { return m_UUID; }
		constexpr bool IsNull() const { return m_UUID == 0; }

		// Makes UUID generation deterministic (tests, reproducible exports).
		static void Seed(uint64_t seed);

	private:
		uint64_t m_UUID;
	};

}

namespace std {
	template<>
	struct hash<Starfall::UUID>
	{
		size_t operator()(const Starfall::UUID& uuid) const { return hash<uint64_t>()(static_cast<uint64_t>(uuid)); }
	};
}
