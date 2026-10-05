#include "FileDialog.h"

#include <imgui.h>

#include <algorithm>

namespace StarfallEditor {

	namespace fs = std::filesystem;

	void FileDialog::Open(const std::string& title, Mode mode, const fs::path& startDirectory, std::vector<std::string> extensions,
		std::function<void(const fs::path&)> onChosen, const std::string& defaultName)
	{
		m_Title = title;
		m_Mode = mode;
		m_Extensions = std::move(extensions);
		m_Callback = std::move(onChosen);
		std::error_code ec;
		m_Directory = fs::is_directory(startDirectory, ec) ? fs::absolute(startDirectory) : fs::current_path();
		std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "%s", m_Directory.string().c_str());
		std::snprintf(m_NameBuffer, sizeof(m_NameBuffer), "%s", defaultName.c_str());
		m_RequestOpen = true;
		m_Open = true;
	}

	void FileDialog::Draw()
	{
		if(!m_Open)
			return;
		std::string popupId = m_Title + "##filedialog";
		if(m_RequestOpen)
		{
			ImGui::OpenPopup(popupId.c_str());
			m_RequestOpen = false;
		}
		ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_Appearing);
		bool keepOpen = true;
		if(ImGui::BeginPopupModal(popupId.c_str(), &keepOpen))
		{
			auto navigate = [&](const fs::path& target) {
				std::error_code ec;
				if(fs::is_directory(target, ec))
				{
					m_Directory = fs::weakly_canonical(target, ec);
					std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "%s", m_Directory.string().c_str());
				}
			};

			if(ImGui::Button("Up") && m_Directory.has_parent_path())
				navigate(m_Directory.parent_path());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			if(ImGui::InputText("##path", m_PathBuffer, sizeof(m_PathBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
				navigate(m_PathBuffer);

			std::vector<fs::directory_entry> entries;
			std::error_code ec;
			for(const auto& entry : fs::directory_iterator(m_Directory, fs::directory_options::skip_permission_denied, ec))
				entries.push_back(entry);
			std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
				std::error_code e;
				bool da = a.is_directory(e), db = b.is_directory(e);
				return da != db ? da : a.path().filename().string() < b.path().filename().string();
			});

			float footer = ImGui::GetFrameHeightWithSpacing() * 2 + 8;
			ImGui::BeginChild("##list", ImVec2(0, -footer), true);
			fs::path chosen;
			for(const auto& entry : entries)
			{
				std::error_code e;
				bool isDirectory = entry.is_directory(e);
				std::string name = entry.path().filename().string();
				if(!isDirectory)
				{
					if(m_Mode == Mode::SelectFolder)
						continue;
					std::string ext = entry.path().extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if(!m_Extensions.empty() && std::find(m_Extensions.begin(), m_Extensions.end(), ext) == m_Extensions.end())
						continue;
				}
				std::string label = (isDirectory ? "[D] " : "    ") + name;
				if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
				{
					if(isDirectory)
					{
						if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
							navigate(entry.path());
					}
					else
					{
						std::snprintf(m_NameBuffer, sizeof(m_NameBuffer), "%s", name.c_str());
						if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
							chosen = entry.path();
					}
				}
			}
			ImGui::EndChild();

			if(m_Mode != Mode::SelectFolder)
			{
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##name", m_NameBuffer, sizeof(m_NameBuffer));
			}
			bool canConfirm = m_Mode == Mode::SelectFolder || m_NameBuffer[0];
			ImGui::BeginDisabled(!canConfirm);
			const char* confirm = m_Mode == Mode::SelectFolder ? "Select This Folder" : m_Mode == Mode::SaveFile ? "Save" : "Open";
			if(ImGui::Button(confirm, ImVec2(160, 0)) && canConfirm)
				chosen = m_Mode == Mode::SelectFolder ? m_Directory : m_Directory / m_NameBuffer;
			ImGui::EndDisabled();
			ImGui::SameLine();
			if(ImGui::Button("Cancel", ImVec2(100, 0)))
				keepOpen = false;

			if(!keepOpen)
				ImGui::CloseCurrentPopup();
			if(!chosen.empty())
			{
				auto callback = std::move(m_Callback);
				keepOpen = false;
				ImGui::CloseCurrentPopup();
				m_Open = false;
				if(callback)
					callback(chosen);
			}
			ImGui::EndPopup();
		}
		if(!keepOpen)
			m_Open = false;
	}

}
