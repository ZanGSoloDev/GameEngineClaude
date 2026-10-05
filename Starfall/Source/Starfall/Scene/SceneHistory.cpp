#include "Starfall/Scene/SceneHistory.h"

namespace Starfall {

	void SceneHistory::Reset(const std::string& snapshot)
	{
		m_States.clear();
		m_States.push_back({ snapshot, "Initial state" });
		m_Cursor = 0;
	}

	bool SceneHistory::Push(const std::string& snapshot, const std::string& label)
	{
		if(m_States.empty())
		{
			Reset(snapshot);
			return true;
		}
		if(m_States[m_Cursor].Snapshot == snapshot)
			return false;
		m_States.resize(m_Cursor + 1); // drop the redo branch
		m_States.push_back({ snapshot, label });
		if(m_States.size() > m_MaxSnapshots)
			m_States.erase(m_States.begin(), m_States.begin() + static_cast<std::ptrdiff_t>(m_States.size() - m_MaxSnapshots));
		m_Cursor = m_States.size() - 1;
		return true;
	}

	const std::string* SceneHistory::Undo()
	{
		if(!CanUndo())
			return nullptr;
		m_Cursor--;
		return &m_States[m_Cursor].Snapshot;
	}

	const std::string* SceneHistory::Redo()
	{
		if(!CanRedo())
			return nullptr;
		m_Cursor++;
		return &m_States[m_Cursor].Snapshot;
	}

	const std::string& SceneHistory::GetUndoLabel() const
	{
		static const std::string none;
		return CanUndo() ? m_States[m_Cursor].Label : none;
	}

	const std::string& SceneHistory::GetRedoLabel() const
	{
		static const std::string none;
		return CanRedo() ? m_States[m_Cursor + 1].Label : none;
	}

}
