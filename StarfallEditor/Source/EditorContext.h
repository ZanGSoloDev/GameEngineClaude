#pragma once

#include "Starfall/Core/UUID.h"
#include "Starfall/Renderer/SceneRenderer.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/Scene.h"
#include "Starfall/Scene/SceneHistory.h"

#include <imgui.h>

#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace StarfallEditor {

	using namespace Starfall;

	enum class GizmoOperation { None = 0, Translate, Rotate, Scale };

	struct ConsoleLine
	{
		LogLevel Level;
		std::string Channel;
		std::string Message;
	};

	// State and services shared between the editor panels.
	struct EditorContext
	{
		Ref<Scene> EditScene;
		Ref<Scene> RuntimeScene; // non-null while playing
		bool Paused = false;
		UUID Selected = UUID(0);
		std::filesystem::path ScenePath;
		bool Dirty = false;
		SceneHistory History;

		// View options
		bool ShowGrid = true;
		bool ShowColliders = false;
		bool ShowLightGizmos = true;
		bool EnableShadows = true;
		bool EnableSSAO = true;
		bool VSync = true;
		GizmoOperation Gizmo = GizmoOperation::Translate;
		bool GizmoLocal = false;
		bool Snap = false;
		float SnapTranslate = 0.5f;
		float SnapRotateDegrees = 15.0f;
		float SnapScale = 0.1f;
		bool GameView = false; // while playing: show the game camera instead of the editor camera

		// Statistics
		RenderStats Stats;
		float FrameMilliseconds = 0.0f;
		std::string AdapterName;

		std::deque<ConsoleLine> Console;
		bool ConsoleDirty = false;

		// Services provided by the editor application
		std::function<void(const std::string& label)> Commit;               // record an undo step for the edit scene
		std::function<void(Entity)> FocusEntity;
		std::function<void(const AssetPath&)> OpenScene;
		std::function<ImTextureID(const AssetPath&)> GetThumbnail;           // 0 if not (yet) available
		std::function<void(const AssetPath&)> AssignAssetToSelection;
		std::function<void(const std::string& message)> ShowToast;

		bool IsPlaying() const { return RuntimeScene != nullptr; }
		Scene& GetScene() { return RuntimeScene ? *RuntimeScene : *EditScene; }
		Entity GetSelectedEntity() { return Selected.IsNull() ? Entity() : GetScene().FindEntityByUUID(Selected); }
		void Select(Entity e) { Selected = e ? e.GetUUID() : UUID(0); }
	};

}
