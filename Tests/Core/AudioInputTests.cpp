#include <doctest/doctest.h>

#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Platform/Input.h"
#include "Starfall/Scene/Entity.h"

#include "TestUtils.h"

#include <cmath>
#include <cstring>

using namespace Starfall;

namespace {

	// 16-bit mono PCM sine wave WAV.
	std::vector<uint8_t> MakeWav(float seconds, int sampleRate = 44100)
	{
		uint32_t samples = static_cast<uint32_t>(seconds * sampleRate);
		uint32_t dataSize = samples * 2;
		std::vector<uint8_t> wav(44 + dataSize);
		auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&wav[at], &v, 4); };
		auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&wav[at], &v, 2); };
		std::memcpy(&wav[0], "RIFF", 4);
		put32(4, 36 + dataSize);
		std::memcpy(&wav[8], "WAVEfmt ", 8);
		put32(16, 16);
		put16(20, 1);
		put16(22, 1);
		put32(24, sampleRate);
		put32(28, sampleRate * 2);
		put16(32, 2);
		put16(34, 16);
		std::memcpy(&wav[36], "data", 4);
		put32(40, dataSize);
		for(uint32_t i = 0; i < samples; i++)
		{
			int16_t v = static_cast<int16_t>(std::sin(2.0 * 3.14159265 * 440.0 * i / sampleRate) * 12000);
			std::memcpy(&wav[44 + i * 2], &v, 2);
		}
		return wav;
	}

}

TEST_CASE("Input: press/release edges, per-frame reset and enable flag")
{
	Input::Reset();
	CHECK_FALSE(Input::IsKeyDown(KeyCode::A));
	Input::OnKey(KeyCode::A, true);
	CHECK(Input::IsKeyDown(KeyCode::A));
	CHECK(Input::IsKeyPressed(KeyCode::A));
	Input::OnKey(KeyCode::A, true); // key repeat must not retrigger Pressed
	Input::BeginFrame();
	CHECK(Input::IsKeyDown(KeyCode::A));
	CHECK_FALSE(Input::IsKeyPressed(KeyCode::A));
	Input::OnKey(KeyCode::A, false);
	CHECK(Input::IsKeyReleased(KeyCode::A));
	CHECK_FALSE(Input::IsKeyDown(KeyCode::A));

	Input::OnMouseButton(MouseButton::Left, true);
	CHECK(Input::IsMouseButtonPressed(MouseButton::Left));
	Input::OnMouseMoved(1, 1);
	Input::OnMouseMoved(4, 5);
	CHECK(Input::GetMouseDelta() == glm::vec2(3, 4));
	Input::BeginFrame();
	CHECK(Input::GetMouseDelta() == glm::vec2(0, 0));
	CHECK(Input::GetMousePosition() == glm::vec2(4, 5));

	Input::SetEnabled(false);
	CHECK_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
	Input::SetEnabled(true);
	CHECK(Input::IsMouseButtonDown(MouseButton::Left));

	// Out-of-range input is ignored.
	Input::OnKey(static_cast<KeyCode>(9999), true);
	CHECK_FALSE(Input::IsKeyDown(static_cast<KeyCode>(9999)));
	Input::Reset();
}

TEST_CASE("KeyCode string conversion")
{
	CHECK(KeyCodeFromString("w") == KeyCode::W);
	CHECK(KeyCodeFromString("W") == KeyCode::W);
	CHECK(KeyCodeFromString("7") == KeyCode::D7);
	CHECK(KeyCodeFromString("space") == KeyCode::Space);
	CHECK(KeyCodeFromString("LeftShift") == KeyCode::LeftShift);
	CHECK(KeyCodeFromString("F1") == KeyCode::F1);
	CHECK(KeyCodeFromString("f12") == KeyCode::F12);
	CHECK(KeyCodeFromString("F13") == KeyCode::Unknown);
	CHECK(KeyCodeFromString("") == KeyCode::Unknown);
	CHECK(KeyCodeFromString("banana") == KeyCode::Unknown);
	CHECK(std::string(KeyCodeToString(KeyCode::Space)) == "Space");
	CHECK(std::string(KeyCodeToString(KeyCode::A)) == "A");
	CHECK(std::string(KeyCodeToString(KeyCode::F5)) == "F5");
	CHECK(std::string(KeyCodeToString(KeyCode::F12)) == "F12");
}

TEST_CASE("Log sinks receive formatted messages and respect level")
{
	std::vector<std::string> received;
	uint32_t handle = Log::AddSink([&](LogLevel, std::string_view, std::string_view message) { received.emplace_back(message); });
	LogLevel previous = Log::GetLevel();
	Log::SetLevel(LogLevel::Info);
	SF_INFO("value {0} and {1}", 5, "x");
	SF_TRACE("hidden");
	Log::SetLevel(previous);
	Log::RemoveSink(handle);
	SF_INFO("after removal");
	REQUIRE(received.size() == 1);
	CHECK(received[0] == "value 5 and x");
}

TEST_CASE("Audio: playback state in headless mode")
{
	if(!AudioEngine::Init(true))
	{
		MESSAGE("audio engine unavailable; skipping");
		return;
	}
	TestProject project;
	project.WriteBinary("Audio/beep.wav", MakeWav(0.25f));

	Scene scene;
	Entity source = scene.CreateEntity("Source");
	auto& audio = source.AddComponent<AudioSourceComponent>();
	audio.Clip = "Audio/beep.wav";
	audio.PlayOnStart = true;
	Entity manual = scene.CreateEntity("Manual");
	manual.AddComponent<AudioSourceComponent>().Clip = "Audio/beep.wav";
	Entity missing = scene.CreateEntity("Missing");
	missing.AddComponent<AudioSourceComponent>().Clip = "Audio/nope.wav";
	scene.CreateEntity("Cam").AddComponent<CameraComponent>();

	scene.OnRuntimeStart();
	AudioWorld* world = scene.GetAudioWorld();
	REQUIRE(world);
	CHECK(world->IsPlaying(source));
	CHECK_FALSE(world->IsPlaying(manual));
	CHECK(world->GetActiveSoundCount() == 1);

	CHECK(world->Play(manual));
	CHECK(world->GetActiveSoundCount() == 2);
	world->Stop(manual);
	CHECK_FALSE(world->IsPlaying(manual));
	CHECK_FALSE(world->Play(missing));    // missing clip fails gracefully
	CHECK_FALSE(world->Play(Entity()));   // invalid entity

	world->SetVolume(manual, 0.25f);
	CHECK(manual.GetComponent<AudioSourceComponent>().Volume == 0.25f);
	world->SetPitch(manual, 2.0f);
	CHECK(manual.GetComponent<AudioSourceComponent>().Pitch == 2.0f);

	CHECK(world->PlayOneShot("Audio/beep.wav", { 0, 0, 0 }, 0.5f));
	CHECK_FALSE(world->PlayOneShot("Audio/nope.wav", { 0, 0, 0 }));
	CHECK(world->GetActiveSoundCount() == 2); // source + one-shot

	// Let the 0.25s clip finish.
	for(int i = 0; i < 30; i++)
	{
		AudioEngine::PumpHeadless(2048);
		scene.OnUpdateRuntime(0.016f);
	}
	CHECK(world->GetActiveSoundCount() == 0);
	CHECK_FALSE(world->IsPlaying(source));

	// Destroying a source mid-play must not leave a dangling sound.
	CHECK(world->Play(source));
	scene.DestroyEntity(source);
	AudioEngine::PumpHeadless(1024);
	scene.OnUpdateRuntime(0.016f);

	scene.OnRuntimeStop();
	AudioEngine::SetMasterVolume(0.5f);
	CHECK(AudioEngine::GetMasterVolume() == doctest::Approx(0.5f));
	CHECK(AudioEngine::GetSampleRate() == 48000);
	AudioEngine::Shutdown();
	CHECK_FALSE(AudioEngine::IsInitialized());
}

TEST_CASE("Audio: scene runs without an audio engine")
{
	AudioEngine::Shutdown();
	Scene scene;
	Entity e = scene.CreateEntity("S");
	e.AddComponent<AudioSourceComponent>().PlayOnStart = true;
	scene.OnRuntimeStart();
	CHECK_FALSE(scene.GetAudioWorld()->Play(e));
	scene.OnUpdateRuntime(0.016f);
	scene.OnRuntimeStop();
}
