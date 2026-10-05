#pragma once

#include "../AssetFiles.h"
#include "../EditorContext.h"

namespace StarfallEditor {

	// File system view of the project's Assets folder with an icon per asset kind.
	class ContentBrowserPanel
	{
	public:
		void OnImGui(EditorContext& ctx, bool* open);
		// Call when the project changed.
		void Reset();
		const std::filesystem::path& GetCurrentFolder() const { return m_Current; }

	private:
		void DrawFolderTree(const std::filesystem::path& folder);
		void DrawBreadcrumbs();
		void DrawItem(EditorContext& ctx, const std::filesystem::directory_entry& entry, float size);
		void DrawContextMenu(EditorContext& ctx);
		void NavigateTo(const std::filesystem::path& folder);

		std::filesystem::path m_Current;
		std::vector<std::filesystem::path> m_BackStack;
		std::filesystem::path m_Selected;
		std::filesystem::path m_RenameTarget;
		std::filesystem::path m_DeleteTarget;
		char m_RenameBuffer[256] = {};
		char m_Filter[128] = {};
		float m_ThumbnailSize = 84.0f;
	};

}
