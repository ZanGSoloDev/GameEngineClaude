#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Scene/Components.h"

#include <glm/glm.hpp>

namespace Starfall {

	class Scene;
	class Entity;

	// Process-wide audio device (miniaudio). Headless mode runs the mixer without an output device (tests, servers).
	class AudioEngine
	{
	public:
		static bool Init(bool headless = false);
		static void Shutdown();
		static bool IsInitialized();
		static void SetMasterVolume(float volume);
		static float GetMasterVolume();
		// Headless only: advances the mixer by `frames` PCM frames so playback state progresses without a device.
		static void PumpHeadless(uint32_t frames);
		static uint32_t GetSampleRate();
	};

	// Per-scene audio state, created by Scene::OnRuntimeStart.
	class AudioWorld
	{
	public:
		explicit AudioWorld(Scene* scene);
		~AudioWorld();
		AudioWorld(const AudioWorld&) = delete;
		AudioWorld& operator=(const AudioWorld&) = delete;

		void OnStart();
		void OnStop();
		void OnUpdate(float deltaTime);

		bool Play(Entity entity);
		void Stop(Entity entity);
		bool IsPlaying(Entity entity) const;
		void SetVolume(Entity entity, float volume);
		void SetPitch(Entity entity, float pitch);

		// Fire-and-forget sound. Returns false if the clip could not be loaded.
		bool PlayOneShot(const AssetPath& clip, const glm::vec3& position, float volume = 1.0f, bool spatial = true);

		uint32_t GetActiveSoundCount() const;

	private:
		struct Impl;
		Scope<Impl> m_Impl;
		Scene* m_Scene;
		uint32_t m_DestroyCallback = 0;
	};

}
