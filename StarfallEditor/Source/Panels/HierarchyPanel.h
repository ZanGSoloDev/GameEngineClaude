#pragma once

#include "../EditorContext.h"

namespace StarfallEditor {

	class HierarchyPanel
	{
	public:
		void OnImGui(EditorContext& ctx, bool* open);

	private:
		void DrawNode(EditorContext& ctx, Entity entity);
		void DrawCreateMenu(EditorContext& ctx, Entity parent);
		bool PassesFilter(Entity entity) const;

		UUID m_Renaming = UUID(0);
		char m_RenameBuffer[256] = {};
		bool m_FocusRename = false;
		char m_Filter[128] = {};

		// Operations are applied after the tree has been drawn so the hierarchy is never modified while iterating.
		struct PendingReparent { UUID Child; UUID Parent; };
		std::vector<PendingReparent> m_Reparents;
		std::vector<std::pair<UUID, AssetPath>> m_AssetDrops;
		UUID m_PendingDelete = UUID(0);
		UUID m_PendingDuplicate = UUID(0);
	};

}
