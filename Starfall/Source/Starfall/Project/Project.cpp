#include "Starfall/Project/Project.h"

#include "Starfall/Core/Base.h"
#include "Starfall/Core/FileSystem.h"

#include <nlohmann/json.hpp>

namespace Starfall {

	namespace {
		struct ProjectState
		{
			bool Loaded = false;
			std::string Name;
			std::filesystem::path Directory;
			std::filesystem::path AssetDirectory;
			std::filesystem::path File;
			AssetPath StartScene;
		};

		ProjectState s_Project;
	}

	bool Project::Load(const std::filesystem::path& projectFile)
	{
		auto text = FileSystem::ReadText(projectFile);
		if(!text)
		{
			SF_CORE_ERROR("Could not read project file '{0}'", projectFile.string());
			return false;
		}
		try
		{
			nlohmann::json j = nlohmann::json::parse(*text);
			ProjectState state;
			state.Name = j.value("Name", "Untitled");
			state.StartScene = j.value("StartScene", "");
			state.File = std::filesystem::absolute(projectFile);
			state.Directory = state.File.parent_path();
			state.AssetDirectory = state.Directory / j.value("AssetDirectory", "Assets");
			state.Loaded = true;
			s_Project = std::move(state);
		}
		catch(const std::exception& e)
		{
			SF_CORE_ERROR("Invalid project file '{0}': {1}", projectFile.string(), e.what());
			return false;
		}
		return true;
	}

	bool Project::Create(const std::filesystem::path& directory, const std::string& name)
	{
		std::error_code ec;
		std::filesystem::create_directories(directory / "Assets" / "Scenes", ec);
		std::filesystem::create_directories(directory / "Assets" / "Scripts", ec);
		std::filesystem::create_directories(directory / "Assets" / "Models", ec);
		std::filesystem::create_directories(directory / "Assets" / "Prefabs", ec);
		std::filesystem::create_directories(directory / "Assets" / "Materials", ec);
		if(ec)
			return false;

		s_Project = {};
		s_Project.Name = name;
		s_Project.Directory = std::filesystem::absolute(directory);
		s_Project.AssetDirectory = s_Project.Directory / "Assets";
		s_Project.File = s_Project.Directory / (name + FileExtension);
		s_Project.StartScene = "Scenes/Main.sfscene";
		s_Project.Loaded = true;
		return Save();
	}

	bool Project::Save()
	{
		if(!s_Project.Loaded)
			return false;
		nlohmann::json j;
		j["Name"] = s_Project.Name;
		j["AssetDirectory"] = "Assets";
		j["StartScene"] = s_Project.StartScene;
		return FileSystem::WriteText(s_Project.File, j.dump(4));
	}

	void Project::Unload() { s_Project = {}; }
	bool Project::IsLoaded() { return s_Project.Loaded; }

	void Project::SetAssetDirectory(const std::filesystem::path& directory)
	{
		s_Project = {};
		s_Project.Loaded = true;
		s_Project.Name = "Anonymous";
		s_Project.Directory = std::filesystem::absolute(directory).parent_path();
		s_Project.AssetDirectory = std::filesystem::absolute(directory);
	}

	const std::string& Project::GetName() { return s_Project.Name; }
	std::filesystem::path Project::GetDirectory() { return s_Project.Directory; }
	std::filesystem::path Project::GetAssetDirectory() { return s_Project.AssetDirectory; }
	std::filesystem::path Project::GetProjectFile() { return s_Project.File; }
	const AssetPath& Project::GetStartScene() { return s_Project.StartScene; }
	void Project::SetStartScene(const AssetPath& scene) { s_Project.StartScene = scene; }

	bool Project::IsValidAssetPath(const AssetPath& path)
	{
		if(path.empty())
			return false;
		std::filesystem::path p = std::filesystem::path(path).lexically_normal();
		if(p.is_absolute() || p.has_root_name() || p.has_root_directory())
			return false;
		for(const auto& part : p)
			if(part == "..")
				return false;
		return true;
	}

	std::filesystem::path Project::ResolvePath(const AssetPath& path)
	{
		if(!IsValidAssetPath(path))
			return {};
		return s_Project.AssetDirectory / std::filesystem::path(path).lexically_normal();
	}

	AssetPath Project::MakeAssetPath(const std::filesystem::path& absolute)
	{
		std::error_code ec;
		std::filesystem::path rel = std::filesystem::relative(absolute, s_Project.AssetDirectory, ec);
		if(ec || rel.empty() || *rel.begin() == "..")
			return {};
		return rel.generic_string();
	}

}
