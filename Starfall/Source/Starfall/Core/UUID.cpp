#include "Starfall/Core/UUID.h"

#include <mutex>
#include <random>

namespace Starfall {

	namespace {
		std::mutex s_Mutex;
		std::mt19937_64 s_Engine{ std::random_device{}() };
		std::uniform_int_distribution<uint64_t> s_Distribution(1, UINT64_MAX);
	}

	UUID::UUID()
	{
		std::scoped_lock lock(s_Mutex);
		m_UUID = s_Distribution(s_Engine);
	}

	void UUID::Seed(uint64_t seed)
	{
		std::scoped_lock lock(s_Mutex);
		s_Engine.seed(seed);
	}

}
