#include "ContentBrowserPanel.h"

#include "../EditorActions.h"
#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Scene/SceneSerializer.h"

#include <algorithm>

namespace StarfallEditor {

	namespace fs = std::filesystem;

	namespace {

		fs::path UniquePath(const fs::path& directory, const std::string& stem, const std::string& extension)
		{
			fs::path candidate = directory / (stem + extension);
			for(int i = 1; fs::exists(candidate) && i < 10000; i++)
				candidate = directory / (stem + " (" + std::to_string(i) + ")" + extension);
			return candidate;
		}

		const char* ScriptTemplate = R"(local Script = {}

-- Exported properties (editable per entity in the inspector).
Script.properties = { speed = 1.0 }

function Script:OnCreate()
end

function Script:OnUpdate(dt)
end

return Script
)";

		bool IsInside(const fs::path& child, const fs::path& root)
		{
			std::error_code ec;
			fs::path rel = fs::relative(child, root, ec);
			return !ec && !rel.empty() && *rel.begin() != "..";
		}

	}

	void ContentBrowserPanel::Reset()
	{
		m_Current = Project::GetAssetDirectory();
		m_BackStack.clear();
		m_Selected.clear();
	}

	void ContentBrowserPanel::NavigateTo(const fs::path& folder)
	{
		if(folder == m_Current)
			return;
		m_BackStack.push_back(m_Current);
		m_Current = folder;
		m_Selected.clear();
	}

	void ContentBrowserPanel::DrawFolderTree(const fs::path& folder)
	{
		std::error_code ec;
		std::vector<fs::path> subfolders;
		for(const auto& entry : fs::directory_iterator(folder, ec))
			if(entry.is_directory(ec))
				subfolders.push_back(entry.path());
		std::sort(subfolders.begin(), subfolders.end());

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if(subfolders.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if(folder == m_Current)
			flags |= ImGuiTreeNodeFlags_Selected;
		if(folder == Project::GetAssetDirectory())
			flags |= ImGuiTreeNodeFlags_DefaultOpen;

		std::string label = folder == Project::GetAssetDirectory() ? "Assets" : folder.filename().string();
		bool open = ImGui::TreeNodeEx(folder.string().c_str(), flags, "%s", label.c_str());
		if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			NavigateTo(folder);
		if(open)
		{
			for(const fs::path& sub : subfolders)
				DrawFolderTree(sub);
			ImGui::TreePop();
		}
	}

	void ContentBrowserPanel::DrawBreadcrumbs()
	{
		fs::path root = Project::GetAssetDirectory();
		if(ImGui::Button("<") && !m_BackStack.empty())
		{
			m_Current = m_BackStack.back();
			m_BackStack.pop_back();
			m_Selected.clear();
		}
		ImGui::SameLine();
		if(ImGui::Button("Assets"))
			NavigateTo(root);
		std::error_code ec;
		fs::path rel = fs::relative(m_Current, root, ec);
		fs::path accumulated = root;
		if(!ec && rel != ".")
		{
			for(const auto& part : rel)
			{
				accumulated /= part;
				ImGui::SameLine();
				ImGui::TextUnformatted("/");
				ImGui::SameLine();
				if(ImGui::Button(part.string().c_str()))
					NavigateTo(accumulated);
			}
		}
	}

	void ContentBrowserPanel::DrawItem(EditorContext& ctx, const fs::directory_entry& entry, float size)
	{
		std::error_code ec;
		const fs::path path = entry.path();
		const bool isDirectory = entry.is_directory(ec);
		const AssetKind kind = ClassifyAsset(path, isDirectory);
		const std::string name = path.filename().string();
		const AssetPath assetPath = Project::MakeAssetPath(path);

		ImGui::PushID(name.c_str());
		ImGui::BeginGroup();

		ImVec2 min = ImGui::GetCursorScreenPos();
		ImVec2 max(min.x + size, min.y + size);
		bool selected = m_Selected == path;
		ImGui::InvisibleButton("##item", ImVec2(size, size + ImGui::GetTextLineHeightWithSpacing() * 2));
		bool hovered = ImGui::IsItemHovered();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		if(selected || hovered)
			dl->AddRectFilled(min, ImVec2(max.x, max.y + ImGui::GetTextLineHeightWithSpacing() * 2), selected ? IM_COL32(70, 110, 190, 120) : IM_COL32(255, 255, 255, 25), 6.0f);

		ImVec2 pad(size * 0.1f, size * 0.1f);
		ImVec2 iconMin(min.x + pad.x, min.y + pad.y), iconMax(max.x - pad.x, max.y - pad.y);
		ImTextureID thumbnail = (kind == AssetKind::Texture && ctx.GetThumbnail) ? ctx.GetThumbnail(assetPath) : ImTextureID_Invalid;
		if(thumbnail != ImTextureID_Invalid)
		{
			dl->AddRectFilled(iconMin, iconMax, IM_COL32(35, 35, 40, 255), 4.0f);
			dl->AddImage(ImTextureRef(thumbnail), iconMin, iconMax);
			dl->AddRect(iconMin, iconMax, IM_COL32(20, 22, 28, 255), 4.0f);
		}
		else
		{
			DrawAssetIcon(dl, kind, iconMin, iconMax);
		}

		// Name (wrapped to two lines, clipped)
		ImVec2 textPos(min.x + 2, max.y + 2);
		dl->PushClipRect(textPos, ImVec2(max.x - 2, max.y + ImGui::GetTextLineHeightWithSpacing() * 2), true);
		bool renaming = m_RenameTarget == path;
		if(!renaming)
		{
			float wrap = size - 4;
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, ImGui::GetColorU32(ImGuiCol_Text), name.c_str(), nullptr, wrap);
		}
		dl->PopClipRect();

		if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			m_Selected = path;
		if(hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			if(isDirectory)
				NavigateTo(path);
			else if(kind == AssetKind::Scene && ctx.OpenScene)
				ctx.OpenScene(assetPath);
			else if(kind == AssetKind::Prefab || kind == AssetKind::Model)
				Actions::Instantiate(ctx, assetPath);
		}

		if(!isDirectory && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("ASSET", assetPath.c_str(), assetPath.size() + 1);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(48, 48));
			DrawAssetIcon(ImGui::GetWindowDrawList(), kind, cursor, ImVec2(cursor.x + 48, cursor.y + 48));
			ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}

		if(ImGui::BeginPopupContextItem("##itemctx"))
		{
			m_Selected = path;
			if(kind == AssetKind::Scene && ImGui::MenuItem("Open") && ctx.OpenScene)
				ctx.OpenScene(assetPath);
			if((kind == AssetKind::Prefab || kind == AssetKind::Model) && ImGui::MenuItem("Instantiate"))
				Actions::Instantiate(ctx, assetPath);
			if(ImGui::MenuItem("Rename"))
			{
				m_RenameTarget = path;
				std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
			}
			if(ImGui::MenuItem("Delete"))
				m_DeleteTarget = path;
			ImGui::EndPopup();
		}

		if(renaming)
		{
			ImGui::SetCursorScreenPos(textPos);
			ImGui::SetNextItemWidth(size);
			ImGui::SetKeyboardFocusHere();
			if(ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				fs::path target = path.parent_path() / m_RenameBuffer;
				std::error_code renameEc;
				if(m_RenameBuffer[0] && !fs::exists(target))
				{
					fs::rename(path, target, renameEc);
					if(!renameEc)
					{
						AssetManager::Invalidate(assetPath);
						m_Selected = target;
					}
				}
				m_RenameTarget.clear();
			}
			else if(ImGui::IsItemDeactivated())
				m_RenameTarget.clear();
		}

		ImGui::EndGroup();
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawContextMenu(EditorContext& ctx)
	{
		if(ImGui::MenuItem("New Folder"))
		{
			std::error_code ec;
			fs::create_directories(UniquePath(m_Current, "New Folder", ""), ec);
		}
		ImGui::Separator();
		if(ImGui::MenuItem("New Script"))
			FileSystem::WriteText(UniquePath(m_Current, "NewScript", ".lua"), ScriptTemplate);
		if(ImGui::MenuItem("New Material"))
			MaterialSerializer::Save(Material("New Material"), UniquePath(m_Current, "NewMaterial", ".sfmat"));
		if(ImGui::MenuItem("New Scene"))
		{
			Scene scene;
			Entity camera = scene.CreateEntity("Main Camera");
			camera.AddComponent<CameraComponent>();
			camera.Transform().Translation = { 0, 2, 8 };
			Entity sun = scene.CreateEntity("Directional Light");
			sun.AddComponent<LightComponent>().Type = LightType::Directional;
			sun.GetComponent<LightComponent>().Intensity = 3.0f;
			sun.Transform().Rotation = { -0.9f, 0.5f, 0.0f };
			SceneSerializer::Serialize(scene, UniquePath(m_Current, "NewScene", ".sfscene"));
		}
		(void)ctx;
	}

	void ContentBrowserPanel::OnImGui(EditorContext& ctx, bool* open)
	{
		if(!ImGui::Begin("Content Browser", open))
		{
			ImGui::End();
			return;
		}
		if(!Project::IsLoaded())
		{
			ImGui::TextDisabled("No project loaded");
			ImGui::End();
			return;
		}
		fs::path root = Project::GetAssetDirectory();
		std::error_code ec;
		if(m_Current.empty() || !fs::is_directory(m_Current, ec) || !IsInside(m_Current, root))
			m_Current = root;

		DrawBreadcrumbs();
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 260.0f);
		ImGui::SetNextItemWidth(150);
		ImGui::InputTextWithHint("##filter", "Search...", m_Filter, sizeof(m_Filter));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90);
		ImGui::SliderFloat("##size", &m_ThumbnailSize, 48.0f, 160.0f, "");
		ImGui::Separator();

		ImGui::BeginChild("##folders", ImVec2(160, 0), true);
		DrawFolderTree(root);
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("##items", ImVec2(0, 0), false);
		std::vector<fs::directory_entry> entries;
		for(const auto& entry : fs::directory_iterator(m_Current, ec))
			entries.push_back(entry);
		std::sort(entries.begin(), entries.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
			std::error_code e;
			bool da = a.is_directory(e), db = b.is_directory(e);
			if(da != db)
				return da;
			return a.path().filename().string() < b.path().filename().string();
		});

		float cell = m_ThumbnailSize + 16.0f;
		int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
		int index = 0;
		std::string filter = m_Filter;
		std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for(const auto& entry : entries)
		{
			std::string name = entry.path().filename().string();
			if(entry.path().extension() == ".tmp")
				continue;
			if(!filter.empty())
			{
				std::string lower = name;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if(lower.find(filter) == std::string::npos)
					continue;
			}
			if(index % columns != 0)
				ImGui::SameLine();
			DrawItem(ctx, entry, m_ThumbnailSize);
			index++;
		}

		// Empty-space context menu
		if(ImGui::BeginPopupContextWindow("##contentctx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
		{
			DrawContextMenu(ctx);
			ImGui::EndPopup();
		}
		if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
			m_Selected.clear();

		// External file drops are handled by the application; internal drops onto folders move files.
		ImGui::EndChild();

		if(!m_DeleteTarget.empty())
		{
			ImGui::OpenPopup("Delete Asset?");
		}
		if(ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Permanently delete '%s'?", m_DeleteTarget.filename().string().c_str());
			ImGui::TextDisabled("This cannot be undone.");
			if(ImGui::Button("Delete", ImVec2(100, 0)))
			{
				std::error_code removeEc;
				AssetPath asset = Project::MakeAssetPath(m_DeleteTarget);
				fs::remove_all(m_DeleteTarget, removeEc);
				if(!asset.empty())
					AssetManager::Invalidate(asset);
				if(m_Selected == m_DeleteTarget)
					m_Selected.clear();
				m_DeleteTarget.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel", ImVec2(100, 0)))
			{
				m_DeleteTarget.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::End();
	}

}
