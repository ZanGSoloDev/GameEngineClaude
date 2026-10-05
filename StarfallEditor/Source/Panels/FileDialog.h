#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace StarfallEditor {

	// Minimal in-editor file/folder picker (no native dialog dependency).
	class FileDialog
	{
	public:
		enum class Mode { OpenFile, SelectFolder, SaveFile };

		// `extensions` are lowercase with a dot (empty = all files). The callback runs once with the chosen path.
		void Open(const std::string& title, Mode mode, const std::filesystem::path& startDirectory, std::vector<std::string> extensions,
			std::function<void(const std::filesystem::path&)> onChosen, const std::string& defaultName = "");
		void Draw();
		bool IsOpen() const { return m_Open; }

	private:
		bool m_Open = false;
		bool m_RequestOpen = false;
		std::string m_Title;
		Mode m_Mode = Mode::OpenFile;
		std::filesystem::path m_Directory;
		std::vector<std::string> m_Extensions;
		std::function<void(const std::filesystem::path&)> m_Callback;
		char m_PathBuffer[1024] = {};
		char m_NameBuffer[256] = {};
	};

}
