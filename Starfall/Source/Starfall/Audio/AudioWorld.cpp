#include "Starfall/Audio/AudioWorld.h"

#include "Starfall/Project/Project.h"
#include "Starfall/Scene/Entity.h"

#include <miniaudio.h>

#include <filesystem>
#include <list>
#include <unordered_map>

namespace Starfall {

	namespace {

		ma_engine* s_Engine = nullptr;
		bool s_Headless = false;

		constexpr uintmax_t StreamThresholdBytes = 2 * 1024 * 1024;

		// Loads a sound from a project file; returns null on failure. Sounds are heap allocated because ma_sound must not move.
		Scope<ma_sound> LoadSound(const AssetPath& clip)
		{
			if(!s_Engine || clip.empty())
				return nullptr;
			std::filesystem::path file = Project::ResolvePath(clip);
			std::error_code ec;
			if(file.empty() || !std::filesystem::is_regular_file(file, ec))
			{
				SF_CORE_WARN("Audio clip not found: '{0}'", clip);
				return nullptr;
			}

			ma_uint32 flags = std::filesystem::file_size(file, ec) > StreamThresholdBytes ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
			auto sound = CreateScope<ma_sound>();
#if defined(_WIN32)
			ma_result result = ma_sound_init_from_file_w(s_Engine, file.wstring().c_str(), flags, nullptr, nullptr, sound.get());
#else
			ma_result result = ma_sound_init_from_file(s_Engine, file.string().c_str(), flags, nullptr, nullptr, sound.get());
#endif
			if(result != MA_SUCCESS)
			{
				SF_CORE_ERROR("Failed to load audio clip '{0}': {1}", clip, ma_result_description(result));
				return nullptr;
			}
			return sound;
		}

		void ConfigureSpatial(ma_sound& sound, bool spatial, float minDistance, float maxDistance)
		{
			ma_sound_set_spatialization_enabled(&sound, spatial ? MA_TRUE : MA_FALSE);
			ma_sound_set_min_distance(&sound, minDistance);
			ma_sound_set_max_distance(&sound, maxDistance);
			ma_sound_set_attenuation_model(&sound, ma_attenuation_model_inverse);
		}

		glm::vec3 Forward(const glm::mat4& world)
		{
			glm::vec3 f = -glm::vec3(world[2]);
			float length = glm::length(f);
			return length > 1e-6f ? f / length : glm::vec3(0, 0, -1);
		}

	}

	bool AudioEngine::Init(bool headless)
	{
		if(s_Engine)
			return true;
		ma_engine_config config = ma_engine_config_init();
		if(headless)
		{
			config.noDevice = MA_TRUE;
			config.channels = 2;
			config.sampleRate = 48000;
		}
		auto* engine = new ma_engine();
		ma_result result = ma_engine_init(&config, engine);
		if(result != MA_SUCCESS)
		{
			SF_CORE_WARN("Audio engine unavailable ({0}); sound is disabled", ma_result_description(result));
			delete engine;
			return false;
		}
		s_Engine = engine;
		s_Headless = headless;
		return true;
	}

	void AudioEngine::Shutdown()
	{
		if(!s_Engine)
			return;
		ma_engine_uninit(s_Engine);
		delete s_Engine;
		s_Engine = nullptr;
	}

	bool AudioEngine::IsInitialized() { return s_Engine != nullptr; }

	void AudioEngine::SetMasterVolume(float volume)
	{
		if(s_Engine)
			ma_engine_set_volume(s_Engine, std::clamp(volume, 0.0f, 4.0f));
	}

	float AudioEngine::GetMasterVolume() { return s_Engine ? ma_engine_get_volume(s_Engine) : 0.0f; }

	uint32_t AudioEngine::GetSampleRate() { return s_Engine ? ma_engine_get_sample_rate(s_Engine) : 0; }

	void AudioEngine::PumpHeadless(uint32_t frames)
	{
		if(!s_Engine || !s_Headless)
			return;
		std::vector<float> buffer(static_cast<size_t>(frames) * 2);
		ma_uint64 read = 0;
		ma_engine_read_pcm_frames(s_Engine, buffer.data(), frames, &read);
	}

	struct AudioWorld::Impl
	{
		std::unordered_map<UUID, Scope<ma_sound>> Sources;
		std::list<Scope<ma_sound>> OneShots;
	};

	AudioWorld::AudioWorld(Scene* scene)
		: m_Impl(CreateScope<Impl>()), m_Scene(scene)
	{
	}

	AudioWorld::~AudioWorld()
	{
		OnStop();
	}

	void AudioWorld::OnStart()
	{
		m_DestroyCallback = m_Scene->AddDestroyCallback([this](Entity entity) {
			auto it = m_Impl->Sources.find(entity.GetUUID());
			if(it != m_Impl->Sources.end())
			{
				ma_sound_uninit(it->second.get());
				m_Impl->Sources.erase(it);
			}
		});

		m_Scene->Each<AudioSourceComponent>([&](Entity entity, AudioSourceComponent& source) {
			if(source.PlayOnStart)
				Play(entity);
		});
	}

	void AudioWorld::OnStop()
	{
		if(m_DestroyCallback)
		{
			m_Scene->RemoveDestroyCallback(m_DestroyCallback);
			m_DestroyCallback = 0;
		}
		for(auto& [id, sound] : m_Impl->Sources)
			ma_sound_uninit(sound.get());
		m_Impl->Sources.clear();
		for(auto& sound : m_Impl->OneShots)
			ma_sound_uninit(sound.get());
		m_Impl->OneShots.clear();
	}

	void AudioWorld::OnUpdate(float)
	{
		if(!s_Engine)
			return;

		// Listener: first active AudioListener, else primary camera.
		Entity listener;
		m_Scene->Each<AudioListenerComponent>([&](Entity entity, AudioListenerComponent& l) {
			if(l.Active && !listener)
				listener = entity;
		});
		if(!listener)
			listener = m_Scene->GetPrimaryCameraEntity();
		if(listener)
		{
			glm::mat4 world = m_Scene->GetWorldTransform(listener);
			glm::vec3 position(world[3]);
			glm::vec3 forward = Forward(world);
			ma_engine_listener_set_position(s_Engine, 0, position.x, position.y, position.z);
			ma_engine_listener_set_direction(s_Engine, 0, forward.x, forward.y, forward.z);
			ma_engine_listener_set_world_up(s_Engine, 0, 0.0f, 1.0f, 0.0f);
		}

		for(auto& [id, sound] : m_Impl->Sources)
		{
			Entity entity = m_Scene->FindEntityByUUID(id);
			if(!entity)
				continue;
			glm::vec3 position(m_Scene->GetWorldTransform(entity)[3]);
			ma_sound_set_position(sound.get(), position.x, position.y, position.z);
		}

		m_Impl->OneShots.remove_if([](Scope<ma_sound>& sound) {
			if(ma_sound_at_end(sound.get()))
			{
				ma_sound_uninit(sound.get());
				return true;
			}
			return false;
		});
	}

	bool AudioWorld::Play(Entity entity)
	{
		auto* source = entity ? entity.TryGetComponent<AudioSourceComponent>() : nullptr;
		if(!source || !s_Engine)
			return false;

		auto it = m_Impl->Sources.find(entity.GetUUID());
		if(it == m_Impl->Sources.end())
		{
			Scope<ma_sound> sound = LoadSound(source->Clip);
			if(!sound)
				return false;
			it = m_Impl->Sources.emplace(entity.GetUUID(), std::move(sound)).first;
			source->RuntimeSoundID = 1;
		}

		ma_sound* sound = it->second.get();
		ConfigureSpatial(*sound, source->Spatial, source->MinDistance, source->MaxDistance);
		ma_sound_set_looping(sound, source->Loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_volume(sound, source->Volume);
		ma_sound_set_pitch(sound, source->Pitch);
		glm::vec3 position(m_Scene->GetWorldTransform(entity)[3]);
		ma_sound_set_position(sound, position.x, position.y, position.z);
		ma_sound_seek_to_pcm_frame(sound, 0);
		return ma_sound_start(sound) == MA_SUCCESS;
	}

	void AudioWorld::Stop(Entity entity)
	{
		if(!entity)
			return;
		auto it = m_Impl->Sources.find(entity.GetUUID());
		if(it != m_Impl->Sources.end())
			ma_sound_stop(it->second.get());
	}

	bool AudioWorld::IsPlaying(Entity entity) const
	{
		if(!entity)
			return false;
		auto it = m_Impl->Sources.find(entity.GetUUID());
		return it != m_Impl->Sources.end() && ma_sound_is_playing(it->second.get());
	}

	void AudioWorld::SetVolume(Entity entity, float volume)
	{
		if(auto* source = entity ? entity.TryGetComponent<AudioSourceComponent>() : nullptr)
			source->Volume = std::max(volume, 0.0f);
		if(!entity)
			return;
		auto it = m_Impl->Sources.find(entity.GetUUID());
		if(it != m_Impl->Sources.end())
			ma_sound_set_volume(it->second.get(), std::max(volume, 0.0f));
	}

	void AudioWorld::SetPitch(Entity entity, float pitch)
	{
		if(auto* source = entity ? entity.TryGetComponent<AudioSourceComponent>() : nullptr)
			source->Pitch = std::max(pitch, 0.01f);
		if(!entity)
			return;
		auto it = m_Impl->Sources.find(entity.GetUUID());
		if(it != m_Impl->Sources.end())
			ma_sound_set_pitch(it->second.get(), std::max(pitch, 0.01f));
	}

	bool AudioWorld::PlayOneShot(const AssetPath& clip, const glm::vec3& position, float volume, bool spatial)
	{
		Scope<ma_sound> sound = LoadSound(clip);
		if(!sound)
			return false;
		ConfigureSpatial(*sound, spatial, 1.0f, 50.0f);
		ma_sound_set_volume(sound.get(), volume);
		ma_sound_set_position(sound.get(), position.x, position.y, position.z);
		if(ma_sound_start(sound.get()) != MA_SUCCESS)
		{
			ma_sound_uninit(sound.get());
			return false;
		}
		m_Impl->OneShots.push_back(std::move(sound));
		return true;
	}

	uint32_t AudioWorld::GetActiveSoundCount() const
	{
		uint32_t count = 0;
		for(const auto& [id, sound] : m_Impl->Sources)
			if(ma_sound_is_playing(sound.get()))
				count++;
		for(const auto& sound : m_Impl->OneShots)
			if(ma_sound_is_playing(sound.get()))
				count++;
		return count;
	}

}
