#pragma once

#include "Starfall/Core/Application.h"
#include "Starfall/Renderer/SceneRenderer.h"
#include "Starfall/Scene/Scene.h"

namespace Starfall {

	// Standalone game player: loads a project, runs its start scene and renders the primary camera.
	// Used by the runtime executable and by exported games.
	class GameApplication : public Application
	{
	public:
		GameApplication(ApplicationDesc desc, std::filesystem::path projectFile);
		~GameApplication() override;

		Scene* GetScene() { return m_Scene.get(); }

	protected:
		bool OnInit() override;
		void OnUpdate(float deltaTime) override;
		void OnRender(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* backBuffer) override;
		void OnShutdown() override;

	private:
		std::filesystem::path m_ProjectFile;
		Ref<Scene> m_Scene;
		Scope<SceneRenderer> m_Renderer;
	};

	// Builds a RenderCamera from a scene camera entity (falls back to a default view if the entity is invalid).
	RenderCamera MakeRenderCamera(Scene& scene, Entity cameraEntity, float aspectRatio);

}
