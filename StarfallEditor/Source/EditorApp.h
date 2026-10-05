#pragma once

#include "EditorCamera.h"
#include "EditorContext.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/FileDialog.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"

#include "Starfall/Core/Application.h"
#include "Starfall/Renderer/SceneRenderer.h"

#include <unordered_set>

namespace StarfallEditor {

	struct EditorOptions
	{
		std::filesystem::path Project;
		std::string SelectEntity;     // select this entity on startup (automation)
		bool StartPlaying = false;
		bool GameView = false;
		bool ShowColliders = false;
	};

	class EditorApp : public Application
	{
	public:
		EditorApp(ApplicationDesc desc, EditorOptions options);
		~EditorApp() override;

	protected:
		bool OnInit() override;
		void OnUpdate(float deltaTime) override;
		void OnRender(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* backBuffer) override;
		void OnImGui() override;
		void OnShutdown() override;
		void OnFileDrop(const std::string& path) override;

	private:
		// Project / scene management
		bool LoadProject(const std::filesystem::path& projectFile);
		void NewScene();
		void OpenScene(const AssetPath& path);
		bool SaveScene();
		void SaveSceneAs(const std::filesystem::path& path);
		void Commit(const std::string& label);
		void RestoreSnapshot(const std::string& snapshot);
		void Undo();
		void Redo();
		void Play();
		void Stop();
		void ExportGame(const std::filesystem::path& outputDirectory);
		void ImportFile(const std::filesystem::path& file);
		void Toast(const std::string& message);
		void UpdateWindowTitle();

		// UI
		void SetupStyle();
		void DrawMenuBar();
		void DrawToolbar();
		void DrawViewport();
		void DrawToasts();
		void DrawWelcome();
		void HandleShortcuts();
		void DrawGizmo(const ImVec2& origin, const ImVec2& size);
		void DrawSelectionOverlays();
		void Pick(const glm::vec2& uv);
		Entity PickEntity(const Math::Ray& ray, const glm::vec2& pixel, const glm::vec2& viewportSize);
		void FocusSelection(Entity entity);
		void CollectOutline(Entity entity, std::vector<UUID>& out);
		void HandleViewportDrop(const glm::vec2& uv);

		EditorOptions m_Options;
		EditorContext m_Ctx;
		EditorCamera m_Camera;
		HierarchyPanel m_Hierarchy;
		InspectorPanel m_Inspector;
		ContentBrowserPanel m_ContentBrowser;
		ConsolePanel m_ConsolePanel;
		SettingsPanel m_SettingsPanel;
		FileDialog m_FileDialog;
		Scope<SceneRenderer> m_Renderer;
		uint32_t m_LogSink = 0;

		bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowContent = true, m_ShowConsole = true, m_ShowSettings = true;
		bool m_ShowAbout = false, m_ShowShortcuts = false;
		bool m_RebuildLayout = true;
		int m_FocusInspector = 0;

		// Viewport state
		glm::vec2 m_ViewportSize = { 1280, 720 };
		glm::vec2 m_ViewportOrigin = { 0, 0 };
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;
		bool m_UsingGizmo = false;
		bool m_GizmoWasUsing = false;
		bool m_GameCameraAvailable = false;
		glm::vec2 m_ClickStart = { 0, 0 };
		ImTextureID m_ViewportTexture = ImTextureID_Invalid;

		// Thumbnails requested by the content browser this frame (uploaded in OnRender)
		std::unordered_map<AssetPath, Ref<Texture2D>> m_Thumbnails;
		std::vector<Ref<Texture2D>> m_PendingThumbnails;

		struct ToastMessage { std::string Text; float TimeLeft; };
		std::vector<ToastMessage> m_Toasts;

		bool m_StopRequested = false;
		bool m_AutoStartDone = false;
		std::string m_PendingStartupSelect;
		std::vector<UUID> m_OutlineBuffer;
	};

}
