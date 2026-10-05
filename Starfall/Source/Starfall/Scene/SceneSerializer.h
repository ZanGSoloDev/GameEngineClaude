#pragma once

#include "Starfall/Scene/Scene.h"

#include <filesystem>
#include <string>

namespace Starfall {

	// JSON based scene / prefab (de)serialization. File extensions: .sfscene, .sfprefab.
	class SceneSerializer
	{
	public:
		static constexpr int FormatVersion = 1;

		static std::string SerializeToString(const Scene& scene);
		// Replaces the scene contents. Returns false on malformed input (scene left unchanged).
		static bool DeserializeFromString(Scene& scene, const std::string& text);

		static bool Serialize(const Scene& scene, const std::filesystem::path& file);
		static bool Deserialize(Scene& scene, const std::filesystem::path& file);

		// Prefab = an entity subtree. Instantiating generates new UUIDs.
		static std::string SerializePrefabToString(Scene& scene, Entity root);
		static bool SerializePrefab(Scene& scene, Entity root, const std::filesystem::path& file);
		static Entity DeserializePrefabFromString(Scene& scene, const std::string& text);
		static Entity DeserializePrefab(Scene& scene, const AssetPath& assetPath);
	};

}
