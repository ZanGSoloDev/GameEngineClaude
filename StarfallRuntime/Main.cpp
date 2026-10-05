// Game player. Usage:
//   StarfallRuntime [--project <file.sfproj>] [--screenshot <png>] [--frames <n>] [--hidden] [--no-vsync] [--validation] [--fullscreen]
// Without --project it looks for Game.sfproj (or any .sfproj) next to the executable, which is the layout of an exported game.

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Core/GameApplication.h"
#include "Starfall/Project/Project.h"

#include <cstring>
#include <iostream>

using namespace Starfall;

namespace {

	std::filesystem::path FindProjectNextToExecutable()
	{
		std::filesystem::path dir = FileSystem::GetExecutableDirectory();
		if(std::filesystem::exists(dir / "Game.sfproj"))
			return dir / "Game.sfproj";
		std::error_code ec;
		for(const auto& entry : std::filesystem::directory_iterator(dir, ec))
			if(entry.path().extension() == Project::FileExtension)
				return entry.path();
		return {};
	}

}

int main(int argc, char** argv)
{
	Log::Init();

	ApplicationDesc desc;
	std::filesystem::path project;
	for(int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
		if(arg == "--project") project = next();
		else if(arg == "--screenshot") desc.ScreenshotPath = next();
		else if(arg == "--frames") desc.ExitAfterFrames = static_cast<uint32_t>(std::atoi(next()));
		else if(arg == "--hidden") desc.HiddenWindow = true;
		else if(arg == "--no-vsync") desc.VSync = false;
		else if(arg == "--validation") desc.EnableValidation = true;
		else if(arg == "--fullscreen") desc.Fullscreen = true;
		else if(arg == "--width") desc.Width = static_cast<uint32_t>(std::atoi(next()));
		else if(arg == "--height") desc.Height = static_cast<uint32_t>(std::atoi(next()));
		else
		{
			std::cerr << "unknown argument: " << arg << "\n";
			return 2;
		}
	}
	if(project.empty())
		project = FindProjectNextToExecutable();
	if(project.empty())
	{
		SF_CORE_ERROR("No project found. Pass --project <file.sfproj> or place Game.sfproj next to the executable.");
		return 2;
	}

	desc.Name = "Starfall";
	GameApplication app(desc, project);
	int result = app.Run();
	Log::Shutdown();
	return result;
}
