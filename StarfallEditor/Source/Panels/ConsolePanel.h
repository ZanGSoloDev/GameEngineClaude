#pragma once

#include "../EditorContext.h"

namespace StarfallEditor {

	class ConsolePanel
	{
	public:
		void OnImGui(EditorContext& ctx, bool* open);

	private:
		bool m_ShowTrace = true, m_ShowInfo = true, m_ShowWarn = true, m_ShowError = true;
		bool m_AutoScroll = true;
		char m_Filter[128] = {};
	};

	// Scene / rendering settings, view options and statistics.
	class SettingsPanel
	{
	public:
		void OnImGui(EditorContext& ctx, bool* open);
	};

}
