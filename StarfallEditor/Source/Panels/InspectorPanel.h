#pragma once

#include "../EditorContext.h"

namespace StarfallEditor {

	class InspectorPanel
	{
	public:
		void OnImGui(EditorContext& ctx, bool* open);

	private:
		void DrawEntity(EditorContext& ctx, Entity entity);
		void DrawAddComponentMenu(Entity entity);
		void DrawMaterialEditor(EditorContext& ctx, const AssetPath& path);

		bool m_CommitPending = false;
		std::string m_CommitLabel;
		char m_NewPropertyName[64] = {};
		int m_NewPropertyType = 0;
	};

}
