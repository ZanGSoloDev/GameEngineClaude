#pragma once

#include "Starfall/Scene/Components.h"

#include <filesystem>
#include <string>

namespace Starfall {

	// A project is a directory on disk with a "Project.sfproj" file and an "Assets" subdirectory.
	// All AssetPath values are relative to the asset directory and use forward slashes.
	class Project
	{
	public:
		static constexpr const char* FileExtension = ".sfproj";

		static bool Load(const std::filesystem::path& projectFile);
		static bool Create(const std::filesystem::path& directory, const std::string& name);
		static bool Save();
		static void Unload();
		static bool IsLoaded();

		// Directly sets the asset root (tests, tools) without a project file.
		static void SetAssetDirectory(const std::filesystem::path& directory);

		static const std::string& GetName();
		static std::filesystem::path GetDirectory();
		static std::filesystem::path GetAssetDirectory();
		static std::filesystem::path GetProjectFile();
		static const AssetPath& GetStartScene();
		static void SetStartScene(const AssetPath& scene);

		static std::filesystem::path ResolvePath(const AssetPath& path);
		// Converts an absolute path inside the asset directory to an AssetPath; returns empty if outside.
		static AssetPath MakeAssetPath(const std::filesystem::path& absolute);
		// Rejects paths that escape the asset directory ("..", absolute paths).
		static bool IsValidAssetPath(const AssetPath& path);
	};

}
