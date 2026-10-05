// Starfall Editor. Usage:
//   StarfallEditor [--project <file.sfproj>] [--select <entity>] [--play] [--game-view] [--colliders]
//                  [--screenshot <png> --frames <n>] [--width <w> --height <h>] [--validation]
//   StarfallEditor --export <file.sfproj> <output folder>      (headless export, no window)

#include "EditorApp.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/ProjectExporter.h"

#include <iostream>

using namespace Starfall;

int main(int argc, char** argv)
{
	Log::Init();

	StarfallEditor::EditorOptions options;
	ApplicationDesc desc;
	desc.Name = "Starfall Editor";
	desc.UseImGui = true;
	desc.Maximized = false;

	for(int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
		if(arg == "--export")
		{
			std::filesystem::path project = next();
			std::filesystem::path out = next();
#if defined(_WIN32)
			std::filesystem::path runtime = FileSystem::GetExecutableDirectory() / "StarfallRuntime.exe";
#else
			std::filesystem::path runtime = FileSystem::GetExecutableDirectory() / "StarfallRuntime";
#endif
			ExportResult result = ExportProject(project, out, runtime, FileSystem::GetResourcesDirectory());
			std::cout << result.Message << "\n";
			return result.Success ? 0 : 1;
		}
		else if(arg == "--project") options.Project = next();
		else if(arg == "--select") options.SelectEntity = next();
		else if(arg == "--play") options.StartPlaying = true;
		else if(arg == "--game-view") options.GameView = true;
		else if(arg == "--colliders") options.ShowColliders = true;
		else if(arg == "--screenshot") desc.ScreenshotPath = next();
		else if(arg == "--frames") desc.ExitAfterFrames = static_cast<uint32_t>(std::atoi(next()));
		else if(arg == "--width") desc.Width = static_cast<uint32_t>(std::atoi(next()));
		else if(arg == "--height") desc.Height = static_cast<uint32_t>(std::atoi(next()));
		else if(arg == "--validation") desc.EnableValidation = true;
		else if(arg == "--no-vsync") desc.VSync = false;
		else
		{
			std::cerr << "unknown argument: " << arg << "\n";
			return 2;
		}
	}

	StarfallEditor::EditorApp app(desc, options);
	int result = app.Run();
	Log::Shutdown();
	return result;
}
