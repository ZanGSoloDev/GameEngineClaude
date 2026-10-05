#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Starfall {

	// Snapshot based undo/redo. Every committed edit pushes the full serialized scene; undo/redo returns the snapshot to restore.
	// Simple and robust (no per-operation command bookkeeping); memory is bounded by `maxSnapshots`.
	class SceneHistory
	{
	public:
		explicit SceneHistory(size_t maxSnapshots = 100) : m_MaxSnapshots(maxSnapshots) {}

		// Starts a new history whose only state is `snapshot` (scene load / new scene).
		void Reset(const std::string& snapshot);
		// Records a new state after an edit. Returns false (and records nothing) if it equals the current state.
		bool Push(const std::string& snapshot, const std::string& label);

		bool CanUndo() const { return m_Cursor > 0; }
		bool CanRedo() const { return m_Cursor + 1 < m_States.size(); }
		// Moves back/forward and returns the snapshot to restore, or null if not possible.
		const std::string* Undo();
		const std::string* Redo();

		const std::string& GetUndoLabel() const;   // label of the edit that Undo would revert
		const std::string& GetRedoLabel() const;
		size_t GetCount() const { return m_States.size(); }

	private:
		struct State
		{
			std::string Snapshot;
			std::string Label;
		};
		std::vector<State> m_States;
		size_t m_Cursor = 0;
		size_t m_MaxSnapshots;
	};

}
