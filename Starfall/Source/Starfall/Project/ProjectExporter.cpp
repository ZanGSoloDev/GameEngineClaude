#include "Starfall/Project/ProjectExporter.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/Project.h"

#include <nlohmann/json.hpp>

namespace Starfall {

	namespace fs = std::filesystem;

	ExportResult ExportProject(const fs::path& projectFile, const fs::path& outputDirectory, const fs::path& runtimeExecutable, const fs::path& resourcesDirectory)
	{
		ExportResult result;
		std::error_code ec;

		if(!fs::is_regular_file(projectFile, ec))
		{
			result.Message = "Project file not found: " + projectFile.string();
			return result;
		}
		if(!fs::is_regular_file(runtimeExecutable, ec))
		{
			result.Message = "Runtime executable not found: " + runtimeExecutable.string();
			return result;
		}
		if(!fs::is_directory(resourcesDirectory / "Shaders", ec))
		{
			result.Message = "Engine resources not found: " + resourcesDirectory.string();
			return result;
		}

		auto text = FileSystem::ReadText(projectFile);
		nlohmann::json project = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
		if(project.is_discarded() || !project.is_object())
		{
			result.Message = "Project file is not valid";
			return result;
		}
		std::string name = project.value("Name", "Game");
		// Sanitize the executable name.
		for(char& c : name)
			if(!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
				c = '_';
		if(name.empty())
			name = "Game";

		fs::path assetDirectory = projectFile.parent_path() / project.value("AssetDirectory", "Assets");
		if(!fs::is_directory(assetDirectory, ec))
		{
			result.Message = "Asset directory not found: " + assetDirectory.string();
			return result;
		}

		// Refuse to export into the project itself or one of its subdirectories (would copy recursively).
		fs::path out = fs::absolute(outputDirectory);
		fs::path projectDir = fs::absolute(projectFile.parent_path());
		auto rel = fs::relative(out, projectDir, ec);
		if(!ec && !rel.empty() && *rel.begin() != "..")
		{
			result.Message = "Output folder must be outside the project folder";
			return result;
		}

		fs::create_directories(out, ec);
		if(ec)
		{
			result.Message = "Cannot create output folder: " + ec.message();
			return result;
		}

		const auto options = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
		result.Executable = out / (name + runtimeExecutable.extension().string());
		fs::copy_file(runtimeExecutable, result.Executable, fs::copy_options::overwrite_existing, ec);
		if(ec)
		{
			result.Message = "Failed to copy the runtime: " + ec.message();
			return result;
		}
		fs::copy(resourcesDirectory, out / "Resources", options, ec);
		if(ec)
		{
			result.Message = "Failed to copy engine resources: " + ec.message();
			return result;
		}
		fs::remove_all(out / "Assets", ec);
		fs::copy(assetDirectory, out / "Assets", options, ec);
		if(ec)
		{
			result.Message = "Failed to copy project assets: " + ec.message();
			return result;
		}

		// Runtime-side project file always points at the local Assets folder.
		project["AssetDirectory"] = "Assets";
		if(!FileSystem::WriteText(out / "Game.sfproj", project.dump(4)))
		{
			result.Message = "Failed to write the project file";
			return result;
		}

		// Native libraries shipped next to the runtime (if any, e.g. shared C runtime on Linux/macOS).
		for(const auto& entry : fs::directory_iterator(runtimeExecutable.parent_path(), ec))
		{
			std::string ext = entry.path().extension().string();
			if(entry.is_regular_file() && (ext == ".dll" || ext == ".so" || ext == ".dylib"))
				fs::copy_file(entry.path(), out / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
		}

		result.Success = true;
		result.Message = "Exported to " + out.string();
		return result;
	}

}
