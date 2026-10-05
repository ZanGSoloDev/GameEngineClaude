#pragma once

#include <filesystem>
#include <string>

namespace Starfall {

	struct ExportResult
	{
		bool Success = false;
		std::string Message;
		std::filesystem::path Executable;
	};

	// Creates a distributable game folder: <out>/<ProjectName>[.exe], Resources/, Assets/ and the project file.
	// The runtime executable contains no editor code. Existing files in the output folder are overwritten.
	ExportResult ExportProject(const std::filesystem::path& projectFile, const std::filesystem::path& outputDirectory,
		const std::filesystem::path& runtimeExecutable, const std::filesystem::path& resourcesDirectory);

}
