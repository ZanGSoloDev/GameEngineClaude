#include "Starfall/Core/GameApplication.h"

#include "Starfall/Project/Project.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"
#include "Starfall/Scripting/ScriptWorld.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Starfall {

	RenderCamera MakeRenderCamera(Scene& scene, Entity cameraEntity, float aspectRatio)
	{
		RenderCamera camera;
		if(cameraEntity && cameraEntity.HasComponent<CameraComponent>())
		{
			const CameraComponent& cc = cameraEntity.GetComponent<CameraComponent>();
			glm::mat4 world = scene.GetWorldTransform(cameraEntity);
			// Remove scale so the view matrix stays a rigid transform.
			glm::mat4 rigid(1.0f);
			rigid[0] = glm::vec4(glm::normalize(glm::vec3(world[0])), 0.0f);
			rigid[1] = glm::vec4(glm::normalize(glm::vec3(world[1])), 0.0f);
			rigid[2] = glm::vec4(glm::normalize(glm::vec3(world[2])), 0.0f);
			rigid[3] = world[3];
			camera.View = glm::inverse(rigid);
			camera.Projection = cc.GetProjection(aspectRatio);
			camera.Position = glm::vec3(world[3]);
			camera.Near = cc.Projection == ProjectionType::Perspective ? cc.PerspectiveNear : std::max(cc.OrthographicNear, 0.01f);
			camera.Far = cc.Projection == ProjectionType::Perspective ? cc.PerspectiveFar : cc.OrthographicFar;
			camera.Orthographic = cc.Projection == ProjectionType::Orthographic;
		}
		else
		{
			camera.Position = { 0, 2.0f, 6.0f };
			camera.View = glm::lookAt(camera.Position, glm::vec3(0, 0.5f, 0), glm::vec3(0, 1, 0));
			camera.Projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 500.0f);
		}
		return camera;
	}

	GameApplication::GameApplication(ApplicationDesc desc, std::filesystem::path projectFile)
		: Application(std::move(desc)), m_ProjectFile(std::move(projectFile))
	{
	}

	GameApplication::~GameApplication() = default;

	bool GameApplication::OnInit()
	{
		if(!Project::Load(m_ProjectFile))
			return false;
		GetWindow().SetTitle(Project::GetName());

		m_Scene = CreateRef<Scene>();
		if(!Project::GetStartScene().empty())
		{
			if(!SceneSerializer::Deserialize(*m_Scene, Project::ResolvePath(Project::GetStartScene())))
				SF_CORE_WARN("Start scene '{0}' could not be loaded; starting with an empty scene", Project::GetStartScene());
		}

		m_Renderer = CreateScope<SceneRenderer>(GetRenderContext());
		ScriptWorld::SetQuitHandler([this] { Close(); });
		m_Scene->OnRuntimeStart();
		return true;
	}

	void GameApplication::OnUpdate(float deltaTime)
	{
		if(m_Scene)
			m_Scene->OnUpdateRuntime(deltaTime);
	}

	void GameApplication::OnRender(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* backBuffer)
	{
		if(!m_Scene || !m_Renderer || !backBuffer)
			return;
		uint32_t w = GetGraphicsDevice().GetWidth();
		uint32_t h = GetGraphicsDevice().GetHeight();
		if(w == 0 || h == 0)
			return;

		SceneRenderOptions options;
		options.Time = m_Scene->GetTime();
		options.DeltaTime = GetDeltaTime();
		options.Frame = GetFrameIndex();
		RenderCamera camera = MakeRenderCamera(*m_Scene, m_Scene->GetPrimaryCameraEntity(), static_cast<float>(w) / static_cast<float>(h));
		m_Renderer->Render(commandList, *m_Scene, camera, w, h, options);
		GetRenderContext().Blit(commandList, m_Renderer->GetOutput(), backBuffer);
	}

	void GameApplication::OnShutdown()
	{
		ScriptWorld::SetQuitHandler(nullptr);
		if(m_Scene)
			m_Scene->OnRuntimeStop();
		m_Renderer.reset();
		m_Scene.reset();
	}

}
