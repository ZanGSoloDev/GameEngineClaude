#include "EditorApp.h"

#include "AssetFiles.h"
#include "EditorActions.h"

#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Physics/PhysicsWorld.h"
#include "Starfall/Core/FileSystem.h"
#include "Starfall/Core/GameApplication.h"
#include "Starfall/Platform/Input.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Project/ProjectExporter.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Scene/SceneSerializer.h"
#include "Starfall/Scripting/ScriptWorld.h"

#include <ImGuizmo.h>
#include <imgui_internal.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

namespace StarfallEditor {

	namespace fs = std::filesystem;

	EditorApp::EditorApp(ApplicationDesc desc, EditorOptions options)
		: Application(std::move(desc)), m_Options(std::move(options))
	{
	}

	EditorApp::~EditorApp() = default;

	// ------------------------------------------------------------------------------------------------ lifecycle

	bool EditorApp::OnInit()
	{
		SetupStyle();

		m_Ctx.AdapterName = GetGraphicsDevice().GetAdapterName();
		m_LogSink = Log::AddSink([this](LogLevel level, std::string_view channel, std::string_view message) {
			m_Ctx.Console.push_back({ level, std::string(channel), std::string(message) });
			if(m_Ctx.Console.size() > 2000)
				m_Ctx.Console.pop_front();
			m_Ctx.ConsoleDirty = true;
		});

		m_Ctx.Commit = [this](const std::string& label) { Commit(label); };
		m_Ctx.FocusEntity = [this](Entity e) { FocusSelection(e); };
		m_Ctx.OpenScene = [this](const AssetPath& path) { OpenScene(path); };
		m_Ctx.ShowToast = [this](const std::string& message) { Toast(message); };
		m_Ctx.GetThumbnail = [this](const AssetPath& path) -> ImTextureID {
			auto it = m_Thumbnails.find(path);
			if(it == m_Thumbnails.end())
				it = m_Thumbnails.emplace(path, AssetManager::GetTexture(path, true)).first;
			Ref<Texture2D> texture = it->second;
			if(!texture)
				return ImTextureID_Invalid;
			if(!texture->GetTexture())
			{
				m_PendingThumbnails.push_back(texture);
				return ImTextureID_Invalid;
			}
			return GetImGui()->GetTextureID(texture->GetTexture());
		};

		m_Renderer = CreateScope<SceneRenderer>(GetRenderContext());
		m_Ctx.EditScene = CreateRef<Scene>();
		m_Ctx.History.Reset(SceneSerializer::SerializeToString(*m_Ctx.EditScene));

		m_Ctx.ShowColliders = m_Options.ShowColliders;
		m_Ctx.GameView = m_Options.GameView;
		m_PendingStartupSelect = m_Options.SelectEntity;

		fs::path project = m_Options.Project;
		if(project.empty())
		{
			// Development convenience: open the bundled feature test project when running from the build tree.
			fs::path dir = FileSystem::GetExecutableDirectory();
			for(int i = 0; i < 6 && !dir.empty(); i++, dir = dir.parent_path())
			{
				fs::path candidate = dir / "Projects" / "TestProject" / "TestProject.sfproj";
				if(fs::exists(candidate))
				{
					project = candidate;
					break;
				}
			}
		}
		if(!project.empty())
			LoadProject(project);

		ScriptWorld::SetQuitHandler([this] { m_StopRequested = true; });
		return true;
	}

	void EditorApp::OnShutdown()
	{
		if(m_Ctx.IsPlaying())
			Stop();
		ScriptWorld::SetQuitHandler(nullptr);
		Log::RemoveSink(m_LogSink);
		m_Renderer.reset();
		m_Thumbnails.clear();
		m_Ctx.EditScene.reset();
		AssetManager::Clear();
	}

	void EditorApp::Toast(const std::string& message)
	{
		SF_CORE_INFO("{0}", message);
		m_Toasts.push_back({ message, 4.0f });
	}

	void EditorApp::UpdateWindowTitle()
	{
		std::string title = "Starfall Editor";
		if(Project::IsLoaded())
			title += " - " + Project::GetName();
		if(!m_Ctx.ScenePath.empty())
			title += " - " + m_Ctx.ScenePath.filename().string();
		if(m_Ctx.Dirty)
			title += "*";
		if(m_Ctx.IsPlaying())
			title += " [Playing]";
		GetWindow().SetTitle(title);
	}

	// ------------------------------------------------------------------------------------------------ project / scene

	bool EditorApp::LoadProject(const fs::path& projectFile)
	{
		if(m_Ctx.IsPlaying())
			Stop();
		if(!Project::Load(projectFile))
		{
			Toast("Could not open project " + projectFile.string());
			return false;
		}
		AssetManager::Clear();
		m_Thumbnails.clear();
		m_ContentBrowser.Reset();
		m_Ctx.Selected = UUID(0);

		const AssetPath& start = Project::GetStartScene();
		if(!start.empty() && fs::exists(Project::ResolvePath(start)))
			OpenScene(start);
		else
			NewScene();
		Toast("Opened project " + Project::GetName());
		return true;
	}

	void EditorApp::NewScene()
	{
		if(m_Ctx.IsPlaying())
			Stop();
		m_Ctx.EditScene = CreateRef<Scene>();
		Scene& scene = *m_Ctx.EditScene;
		Entity camera = scene.CreateEntity("Main Camera");
		camera.AddComponent<CameraComponent>();
		camera.Transform().Translation = { 0, 2, 8 };
		Entity sun = scene.CreateEntity("Directional Light");
		auto& light = sun.AddComponent<LightComponent>();
		light.Type = LightType::Directional;
		light.Intensity = 3.0f;
		sun.Transform().Rotation = { glm::radians(-50.0f), glm::radians(30.0f), 0.0f };
		m_Ctx.ScenePath.clear();
		m_Ctx.Selected = UUID(0);
		m_Ctx.History.Reset(SceneSerializer::SerializeToString(scene));
		m_Ctx.Dirty = false;
	}

	void EditorApp::OpenScene(const AssetPath& path)
	{
		if(m_Ctx.IsPlaying())
			Stop();
		fs::path file = Project::ResolvePath(path);
		auto scene = CreateRef<Scene>();
		if(file.empty() || !SceneSerializer::Deserialize(*scene, file))
		{
			Toast("Could not open scene " + path);
			return;
		}
		m_Ctx.EditScene = scene;
		m_Ctx.ScenePath = file;
		m_Ctx.Selected = UUID(0);
		m_Ctx.History.Reset(SceneSerializer::SerializeToString(*scene));
		m_Ctx.Dirty = false;
	}

	bool EditorApp::SaveScene()
	{
		if(m_Ctx.IsPlaying())
		{
			Toast("Stop playing before saving");
			return false;
		}
		if(m_Ctx.ScenePath.empty())
		{
			fs::path start = Project::IsLoaded() ? Project::GetAssetDirectory() / "Scenes" : fs::current_path();
			m_FileDialog.Open("Save Scene", FileDialog::Mode::SaveFile, start, { ".sfscene" }, [this](const fs::path& p) { SaveSceneAs(p); }, "Untitled.sfscene");
			return false;
		}
		if(!SceneSerializer::Serialize(*m_Ctx.EditScene, m_Ctx.ScenePath))
		{
			Toast("Failed to save " + m_Ctx.ScenePath.string());
			return false;
		}
		m_Ctx.Dirty = false;
		Toast("Saved " + m_Ctx.ScenePath.filename().string());
		return true;
	}

	void EditorApp::SaveSceneAs(const fs::path& chosen)
	{
		fs::path path = chosen;
		if(path.extension() != ".sfscene")
			path += ".sfscene";
		m_Ctx.ScenePath = path;
		m_Ctx.EditScene->SetName(path.stem().string());
		SaveScene();
		if(Project::IsLoaded() && Project::GetStartScene().empty())
		{
			Project::SetStartScene(Project::MakeAssetPath(path));
			Project::Save();
		}
	}

	void EditorApp::Commit(const std::string& label)
	{
		if(m_Ctx.IsPlaying())
			return;
		m_Ctx.Dirty = true;
		m_Ctx.History.Push(SceneSerializer::SerializeToString(*m_Ctx.EditScene), label);
	}

	void EditorApp::RestoreSnapshot(const std::string& snapshot)
	{
		SceneSerializer::DeserializeFromString(*m_Ctx.EditScene, snapshot);
		if(!m_Ctx.GetSelectedEntity())
			m_Ctx.Selected = UUID(0);
		m_Ctx.Dirty = true;
	}

	void EditorApp::Undo()
	{
		if(m_Ctx.IsPlaying())
			return;
		if(const std::string* snapshot = m_Ctx.History.Undo())
			RestoreSnapshot(*snapshot);
	}

	void EditorApp::Redo()
	{
		if(m_Ctx.IsPlaying())
			return;
		if(const std::string* snapshot = m_Ctx.History.Redo())
			RestoreSnapshot(*snapshot);
	}

	void EditorApp::Play()
	{
		if(m_Ctx.IsPlaying())
			return;
		m_Ctx.RuntimeScene = Scene::Copy(*m_Ctx.EditScene);
		m_Ctx.Paused = false;
		m_Ctx.RuntimeScene->OnRuntimeStart();
		m_Ctx.GameView = m_Ctx.RuntimeScene->GetPrimaryCameraEntity() ? true : m_Ctx.GameView;
		SF_CORE_INFO("Entered play mode");
	}

	void EditorApp::Stop()
	{
		if(!m_Ctx.IsPlaying())
			return;
		m_Ctx.RuntimeScene->OnRuntimeStop();
		m_Ctx.RuntimeScene.reset();
		m_Ctx.Paused = false;
		Input::Reset();
		Input::SetEnabled(true);
		if(!m_Ctx.GetSelectedEntity())
			m_Ctx.Selected = UUID(0);
		SF_CORE_INFO("Left play mode");
	}

	void EditorApp::ExportGame(const fs::path& outputDirectory)
	{
		if(!Project::IsLoaded())
		{
			Toast("No project to export");
			return;
		}
		if(!m_Ctx.IsPlaying() && m_Ctx.Dirty && !m_Ctx.ScenePath.empty())
			SaveScene();
		Project::Save();
#if defined(_WIN32)
		fs::path runtime = FileSystem::GetExecutableDirectory() / "StarfallRuntime.exe";
#else
		fs::path runtime = FileSystem::GetExecutableDirectory() / "StarfallRuntime";
#endif
		ExportResult result = ExportProject(Project::GetProjectFile(), outputDirectory, runtime, FileSystem::GetResourcesDirectory());
		Toast(result.Success ? "Exported game: " + result.Executable.string() : "Export failed: " + result.Message);
	}

	void EditorApp::ImportFile(const fs::path& file)
	{
		if(!Project::IsLoaded())
		{
			Toast("Open a project first");
			return;
		}
		std::error_code ec;
		fs::path destinationDirectory = m_ContentBrowser.GetCurrentFolder().empty() ? Project::GetAssetDirectory() : m_ContentBrowser.GetCurrentFolder();
		fs::path destination = destinationDirectory / file.filename();
		fs::copy_file(file, destination, fs::copy_options::overwrite_existing, ec);
		if(ec)
		{
			Toast("Import failed: " + ec.message());
			return;
		}
		// glTF files reference external buffers/textures next to them: copy siblings of the same stem and common resource files.
		if(file.extension() == ".gltf")
		{
			for(const auto& entry : fs::directory_iterator(file.parent_path(), ec))
			{
				std::string ext = entry.path().extension().string();
				if(entry.is_regular_file() && (ext == ".bin" || ext == ".png" || ext == ".jpg" || ext == ".jpeg"))
					fs::copy_file(entry.path(), destinationDirectory / entry.path().filename(), fs::copy_options::skip_existing, ec);
			}
		}
		AssetManager::Invalidate(Project::MakeAssetPath(destination));
		Toast("Imported " + file.filename().string());
	}

	void EditorApp::OnFileDrop(const std::string& path)
	{
		ImportFile(fs::path(path));
	}

	// ------------------------------------------------------------------------------------------------ update

	void EditorApp::OnUpdate(float deltaTime)
	{
		m_Ctx.FrameMilliseconds = glm::mix(m_Ctx.FrameMilliseconds, deltaTime * 1000.0f, 0.1f);
		GetGraphicsDevice().SetVSync(m_Ctx.VSync);

		bool playing = m_Ctx.IsPlaying();
		// Game input only while the viewport is focused in play mode; the editor uses ImGui input otherwise.
		Input::SetEnabled(!playing || (m_ViewportFocused && m_Ctx.GameView));

		if(m_StopRequested)
		{
			m_StopRequested = false;
			Stop();
			playing = false;
		}

		if(playing)
			m_Ctx.RuntimeScene->OnUpdateRuntime(deltaTime);

		// Editor camera (not while looking through the game camera)
		bool gameView = playing && m_Ctx.GameView;
		if(m_ViewportHovered && !gameView && !m_UsingGizmo)
		{
			ImGuiIO& io = ImGui::GetIO();
			EditorCamera::Input in;
			in.MouseDelta = { io.MouseDelta.x, io.MouseDelta.y };
			in.Scroll = io.MouseWheel;
			in.RightButton = ImGui::IsMouseDown(ImGuiMouseButton_Right);
			in.MiddleButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
			in.LeftButton = ImGui::IsMouseDown(ImGuiMouseButton_Left);
			in.Alt = io.KeyAlt;
			in.Shift = io.KeyShift;
			if(in.RightButton)
			{
				in.Move.x = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
				in.Move.z = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
				in.Move.y = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
			}
			m_Camera.Update(deltaTime, in);
		}
		UpdateWindowTitle();
	}

	// ------------------------------------------------------------------------------------------------ rendering

	void EditorApp::CollectOutline(Entity entity, std::vector<UUID>& out)
	{
		out.push_back(entity.GetUUID());
		for(Entity child : entity.GetChildren())
			CollectOutline(child, out);
	}

	void EditorApp::DrawSelectionOverlays()
	{
		DebugRenderer& debug = m_Renderer->GetDebugRenderer();
		Scene& scene = m_Ctx.GetScene();
		const glm::vec4 yellow(1.0f, 0.85f, 0.25f, 1.0f), green(0.3f, 0.95f, 0.45f, 1.0f), cyan(0.2f, 0.85f, 1.0f, 1.0f), purple(0.8f, 0.5f, 1.0f, 1.0f);

		auto drawCollider = [&](Entity e, const glm::vec4& color) {
			auto* c = e.TryGetComponent<ColliderComponent>();
			if(!c)
				return;
			glm::mat4 world = scene.GetWorldTransform(e);
			glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
			glm::mat4 rigid = world;
			for(int i = 0; i < 3; i++)
				rigid[i] = glm::vec4(glm::normalize(glm::vec3(world[i])), 0.0f);
			glm::vec3 offsetWorld = glm::vec3(rigid * glm::vec4(c->Offset * scale, 1.0f));
			rigid[3] = glm::vec4(offsetWorld, 1.0f);
			if(c->Shape == ColliderShape::Box)
				debug.DrawBox(rigid, c->Size * scale * 0.5f, color, false);
			else if(c->Shape == ColliderShape::Sphere)
				debug.DrawSphere(glm::vec3(rigid[3]), c->Size.x * 0.5f * std::max({ scale.x, scale.y, scale.z }), color, false);
			else
			{
				float radius = c->Size.x * 0.5f * std::max(scale.x, scale.z);
				float height = c->Size.y * scale.y;
				debug.DrawCapsule(rigid, radius, std::max(height - 2.0f * radius, 0.0f), color, false);
			}
		};

		Entity selected = m_Ctx.GetSelectedEntity();

		if(m_Ctx.ShowLightGizmos)
		{
			scene.Each<LightComponent>([&](Entity e, LightComponent& l) {
				glm::mat4 world = scene.GetWorldTransform(e);
				glm::vec3 position(world[3]);
				glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
				glm::vec4 color(glm::vec3(l.Color) * 0.6f + 0.4f, 1.0f);
				bool isSelected = selected == e;
				if(l.Type == LightType::Directional)
				{
					debug.DrawCircle(position, glm::vec3(world[0]), glm::vec3(world[1]), 0.35f, color, false, 24);
					debug.DrawArrow(position, position + forward * (isSelected ? 3.0f : 1.6f), color, false);
					for(int i = 0; i < 6; i++)
					{
						float a = i / 6.0f * glm::two_pi<float>();
						glm::vec3 ray = glm::normalize(glm::vec3(world[0])) * std::cos(a) + glm::normalize(glm::vec3(world[1])) * std::sin(a);
						debug.DrawLine(position + ray * 0.45f, position + ray * 0.65f, color, false);
					}
				}
				else if(l.Type == LightType::Point)
				{
					debug.DrawSphere(position, 0.18f, color, false);
					if(isSelected)
						debug.DrawSphere(position, l.Range, yellow * glm::vec4(1, 1, 1, 0.5f), true);
				}
				else
				{
					debug.DrawSphere(position, 0.15f, color, false);
					debug.DrawCone(position, forward, isSelected ? l.Range : std::min(l.Range, 2.0f), l.OuterConeAngle, color, isSelected ? false : true);
				}
			});

			scene.Each<CameraComponent>([&](Entity e, CameraComponent& c) {
				if(m_Ctx.IsPlaying() && m_Ctx.GameView)
					return;
				glm::mat4 world = scene.GetWorldTransform(e);
				glm::mat4 rigid = world;
				for(int i = 0; i < 3; i++)
					rigid[i] = glm::vec4(glm::normalize(glm::vec3(world[i])), 0.0f);
				float aspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 1.0f);
				CameraComponent preview = c;
				preview.PerspectiveFar = std::min(c.PerspectiveFar, selected == e ? 6.0f : 2.0f);
				debug.DrawFrustum(preview.GetProjection(aspect) * glm::inverse(rigid), green, false);
			});

			scene.Each<AudioSourceComponent>([&](Entity e, AudioSourceComponent& a) {
				glm::vec3 position(scene.GetWorldTransform(e)[3]);
				debug.DrawCircle(position, { 1, 0, 0 }, { 0, 0, 1 }, 0.2f, purple, false, 20);
				debug.DrawCircle(position, { 1, 0, 0 }, { 0, 1, 0 }, 0.2f, purple, false, 20);
				if(selected == e && a.Spatial)
				{
					debug.DrawSphere(position, a.MinDistance, purple * glm::vec4(1, 1, 1, 0.6f), true);
					debug.DrawSphere(position, a.MaxDistance, purple * glm::vec4(1, 1, 1, 0.3f), true);
				}
			});
		}

		if(m_Ctx.ShowColliders)
			scene.Each<ColliderComponent>([&](Entity e, ColliderComponent& c) { drawCollider(e, c.IsTrigger ? yellow : cyan * glm::vec4(1, 1, 1, 0.8f)); });
		if(selected)
			drawCollider(selected, glm::vec4(0.3f, 1.0f, 0.5f, 1.0f));
	}

	void EditorApp::OnRender(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* backBuffer)
	{
		(void)backBuffer;
		commandList->clearTextureFloat(GetGraphicsDevice().GetCurrentBackBuffer(), nvrhi::AllSubresources, nvrhi::Color(0.09f, 0.09f, 0.1f, 1.0f));

		for(const Ref<Texture2D>& texture : m_PendingThumbnails)
			texture->EnsureGPU(GetGraphicsDevice().GetDevice(), commandList);
		m_PendingThumbnails.clear();

		if(!m_Renderer || !Project::IsLoaded() && !m_Ctx.EditScene)
			return;
		uint32_t w = static_cast<uint32_t>(std::max(m_ViewportSize.x, 16.0f));
		uint32_t h = static_cast<uint32_t>(std::max(m_ViewportSize.y, 16.0f));
		Scene& scene = m_Ctx.GetScene();

		bool gameView = m_Ctx.IsPlaying() && m_Ctx.GameView;
		RenderCamera camera;
		Entity gameCamera = scene.GetPrimaryCameraEntity();
		m_GameCameraAvailable = static_cast<bool>(gameCamera);
		if(gameView && gameCamera)
			camera = MakeRenderCamera(scene, gameCamera, static_cast<float>(w) / static_cast<float>(h));
		else
			camera = m_Camera.GetRenderCamera(static_cast<float>(w) / static_cast<float>(h));

		SceneRenderOptions options;
		options.Time = scene.GetTime();
		options.DeltaTime = GetDeltaTime();
		options.Frame = GetFrameIndex();
		options.DrawGrid = m_Ctx.ShowGrid && !gameView;
		options.EnableShadows = m_Ctx.EnableShadows;
		options.EnableSSAO = m_Ctx.EnableSSAO;
		if(!gameView)
		{
			if(Entity selected = m_Ctx.GetSelectedEntity())
				CollectOutline(selected, options.OutlinedEntities);
			DrawSelectionOverlays();
		}
		m_Renderer->Render(commandList, scene, camera, w, h, options);
		m_Ctx.Stats = m_Renderer->GetStats();
		m_ViewportTexture = m_Renderer->GetOutput() ? GetImGui()->GetTextureID(m_Renderer->GetOutput()) : ImTextureID_Invalid;
	}

	// ------------------------------------------------------------------------------------------------ picking

	void EditorApp::FocusSelection(Entity entity)
	{
		if(!entity)
			return;
		glm::vec3 center;
		float radius;
		Actions::GetBounds(m_Ctx.GetScene(), entity, center, radius);
		m_Camera.Focus(center, radius);
	}

	Entity EditorApp::PickEntity(const Math::Ray& ray, const glm::vec2& pixel, const glm::vec2& viewportSize)
	{
		Scene& scene = m_Ctx.GetScene();
		Entity best;
		float bestT = 1e30f;

		scene.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr) {
			if(!mr.Visible)
				return;
			Ref<Mesh> mesh = AssetManager::GetMesh(mr.Mesh);
			if(!mesh)
				return;
			glm::mat4 world = scene.GetWorldTransform(e);
			float boxT;
			if(!Math::IntersectRayAABB(ray, mesh->GetBounds().Transformed(world), boxT) || boxT > bestT)
				return;
			glm::mat4 inverse = glm::inverse(world);
			Math::Ray local;
			local.Origin = glm::vec3(inverse * glm::vec4(ray.Origin, 1.0f));
			local.Direction = glm::vec3(inverse * glm::vec4(ray.Direction, 0.0f)); // not normalized: t stays comparable with world t
			float t;
			if(mesh->Raycast(local, t) && t < bestT)
			{
				bestT = t;
				best = e;
			}
		});

		// Icon-only entities (lights, cameras, audio) are picked by screen distance to their position.
		RenderCamera camera = m_Camera.GetRenderCamera(viewportSize.x / std::max(viewportSize.y, 1.0f));
		glm::mat4 viewProj = camera.Projection * camera.View;
		auto tryIcon = [&](Entity e) {
			glm::vec3 position(scene.GetWorldTransform(e)[3]);
			glm::vec4 clip = viewProj * glm::vec4(position, 1.0f);
			if(clip.w <= 0.0f)
				return;
			glm::vec2 ndc = glm::vec2(clip) / clip.w;
			glm::vec2 screen((ndc.x * 0.5f + 0.5f) * viewportSize.x, (0.5f - ndc.y * 0.5f) * viewportSize.y);
			if(glm::distance(screen, pixel) < 16.0f)
			{
				float t = glm::distance(position, ray.Origin);
				if(t < bestT)
				{
					bestT = t;
					best = e;
				}
			}
		};
		scene.Each<LightComponent>([&](Entity e, LightComponent&) { tryIcon(e); });
		scene.Each<CameraComponent>([&](Entity e, CameraComponent&) { tryIcon(e); });
		scene.Each<AudioSourceComponent>([&](Entity e, AudioSourceComponent&) { tryIcon(e); });
		return best;
	}

	void EditorApp::Pick(const glm::vec2& uv)
	{
		float aspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 1.0f);
		Math::Ray ray = m_Camera.ScreenRay(uv, aspect);
		Entity hit = PickEntity(ray, uv * m_ViewportSize, m_ViewportSize);
		m_Ctx.Select(hit);
	}

	void EditorApp::HandleViewportDrop(const glm::vec2& uv)
	{
		if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
		{
			AssetPath asset(static_cast<const char*>(payload->Data));
			AssetKind kind = ClassifyAsset(asset, false);
			float aspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 1.0f);
			Math::Ray ray = m_Camera.ScreenRay(uv, aspect);
			if(kind == AssetKind::Prefab || kind == AssetKind::Model)
			{
				// Drop on the ground plane under the cursor, or 6 units in front of the camera.
				glm::vec3 position = ray.Origin + ray.Direction * 6.0f;
				if(std::abs(ray.Direction.y) > 1e-4f)
				{
					float t = -ray.Origin.y / ray.Direction.y;
					if(t > 0.0f && t < 200.0f)
						position = ray.Origin + ray.Direction * t;
				}
				if(Entity e = Actions::Instantiate(m_Ctx, asset))
				{
					e.Transform().Translation = position;
					Commit("Place " + e.GetName());
				}
			}
			else if(Entity target = PickEntity(ray, uv * m_ViewportSize, m_ViewportSize))
			{
				Actions::AssignAsset(m_Ctx, target, asset);
			}
			else if(kind == AssetKind::Scene)
			{
				OpenScene(asset);
			}
		}
	}

	// ------------------------------------------------------------------------------------------------ gizmo

	void EditorApp::DrawGizmo(const ImVec2& origin, const ImVec2& size)
	{
		Entity selected = m_Ctx.GetSelectedEntity();
		m_UsingGizmo = false;
		bool gameView = m_Ctx.IsPlaying() && m_Ctx.GameView;
		if(!selected || gameView || m_Ctx.Gizmo == GizmoOperation::None)
		{
			m_GizmoWasUsing = false;
			return;
		}

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);

		RenderCamera camera = m_Camera.GetRenderCamera(size.x / std::max(size.y, 1.0f));
		Scene& scene = m_Ctx.GetScene();
		glm::mat4 world = scene.GetWorldTransform(selected);

		ImGuizmo::OPERATION op = m_Ctx.Gizmo == GizmoOperation::Translate ? ImGuizmo::TRANSLATE : m_Ctx.Gizmo == GizmoOperation::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
		ImGuizmo::MODE mode = (m_Ctx.GizmoLocal || m_Ctx.Gizmo == GizmoOperation::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
		float snapValues[3];
		float snap = m_Ctx.Gizmo == GizmoOperation::Translate ? m_Ctx.SnapTranslate : m_Ctx.Gizmo == GizmoOperation::Rotate ? m_Ctx.SnapRotateDegrees : m_Ctx.SnapScale;
		bool snapping = m_Ctx.Snap || ImGui::GetIO().KeyCtrl;
		snapValues[0] = snapValues[1] = snapValues[2] = snap;

		bool changed = ImGuizmo::Manipulate(glm::value_ptr(camera.View), glm::value_ptr(camera.Projection), op, mode, glm::value_ptr(world), nullptr, snapping ? snapValues : nullptr);
		m_UsingGizmo = ImGuizmo::IsUsing();
		if(changed)
		{
			scene.SetWorldTransform(selected, world);
			if(m_Ctx.IsPlaying())
				if(PhysicsWorld* physics = scene.GetPhysicsWorld())
					physics->Teleport(selected);
			m_Ctx.Dirty = true;
		}
		if(m_GizmoWasUsing && !m_UsingGizmo)
			Commit("Transform " + selected.GetName());
		m_GizmoWasUsing = m_UsingGizmo;
	}

	// ------------------------------------------------------------------------------------------------ UI

	void EditorApp::SetupStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.FontSizeBase = 15.0f;
		style.WindowRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.GrabRounding = 3.0f;
		style.PopupRounding = 4.0f;
		style.TabRounding = 3.0f;
		style.ScrollbarRounding = 6.0f;
		style.WindowPadding = ImVec2(8, 8);
		style.FramePadding = ImVec2(6, 4);
		style.ItemSpacing = ImVec2(8, 5);
		style.WindowMenuButtonPosition = ImGuiDir_None;

		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.11f, 0.125f, 1.0f);
		c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.095f, 0.11f, 1.0f);
		c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.14f, 0.98f);
		c[ImGuiCol_Border] = ImVec4(0.2f, 0.21f, 0.24f, 1.0f);
		c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.21f, 1.0f);
		c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.28f, 1.0f);
		c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.35f, 1.0f);
		c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.085f, 0.1f, 1.0f);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.1f, 0.11f, 0.13f, 1.0f);
		c[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.085f, 0.1f, 1.0f);
		c[ImGuiCol_Header] = ImVec4(0.2f, 0.28f, 0.45f, 0.7f);
		c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.36f, 0.58f, 0.8f);
		c[ImGuiCol_HeaderActive] = ImVec4(0.3f, 0.42f, 0.68f, 1.0f);
		c[ImGuiCol_Button] = ImVec4(0.2f, 0.23f, 0.3f, 1.0f);
		c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.34f, 0.46f, 1.0f);
		c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.42f, 0.62f, 1.0f);
		c[ImGuiCol_Tab] = ImVec4(0.12f, 0.13f, 0.16f, 1.0f);
		c[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.38f, 0.6f, 1.0f);
		c[ImGuiCol_TabSelected] = ImVec4(0.2f, 0.28f, 0.45f, 1.0f);
		c[ImGuiCol_TabDimmed] = ImVec4(0.1f, 0.11f, 0.13f, 1.0f);
		c[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.2f, 0.32f, 1.0f);
		c[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.7f, 1.0f, 1.0f);
		c[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.6f, 0.95f, 1.0f);
		c[ImGuiCol_DockingPreview] = ImVec4(0.3f, 0.5f, 0.9f, 0.5f);
	}

	void EditorApp::HandleShortcuts()
	{
		ImGuiIO& io = ImGui::GetIO();
		if(io.WantTextInput)
			return;
		bool ctrl = io.KeyCtrl;
		if(ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
			SaveScene();
		if(ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			if(io.KeyShift) Redo(); else Undo();
		}
		if(ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			Redo();
		if(ImGui::IsKeyPressed(ImGuiKey_F5, false))
		{
			if(m_Ctx.IsPlaying()) Stop(); else Play();
		}
		bool gameFocused = m_Ctx.IsPlaying() && m_Ctx.GameView && m_ViewportFocused;
		if(m_ViewportHovered && !gameFocused && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			if(ImGui::IsKeyPressed(ImGuiKey_W, false)) m_Ctx.Gizmo = GizmoOperation::Translate;
			if(ImGui::IsKeyPressed(ImGuiKey_E, false)) m_Ctx.Gizmo = GizmoOperation::Rotate;
			if(ImGui::IsKeyPressed(ImGuiKey_R, false)) m_Ctx.Gizmo = GizmoOperation::Scale;
			if(ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_Ctx.Gizmo = GizmoOperation::None;
			if(ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusSelection(m_Ctx.GetSelectedEntity());
			if(ImGui::IsKeyPressed(ImGuiKey_Delete, false)) Actions::Delete(m_Ctx, m_Ctx.GetSelectedEntity());
			if(ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) Actions::Duplicate(m_Ctx, m_Ctx.GetSelectedEntity());
		}
	}

	void EditorApp::DrawMenuBar()
	{
		if(!ImGui::BeginMainMenuBar())
			return;
		if(ImGui::BeginMenu("File"))
		{
			if(ImGui::MenuItem("New Scene")) NewScene();
			if(ImGui::MenuItem("Open Scene...", nullptr, false, Project::IsLoaded()))
				m_FileDialog.Open("Open Scene", FileDialog::Mode::OpenFile, Project::GetAssetDirectory(), { ".sfscene" }, [this](const fs::path& p) { OpenScene(Project::MakeAssetPath(p)); });
			if(ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
			if(ImGui::MenuItem("Save Scene As..."))
				m_FileDialog.Open("Save Scene", FileDialog::Mode::SaveFile, Project::IsLoaded() ? Project::GetAssetDirectory() : fs::current_path(), { ".sfscene" }, [this](const fs::path& p) { SaveSceneAs(p); }, "Untitled.sfscene");
			ImGui::Separator();
			if(ImGui::MenuItem("New Project..."))
				m_FileDialog.Open("Choose Folder For New Project", FileDialog::Mode::SelectFolder, fs::current_path(), {}, [this](const fs::path& folder) {
					std::string name = folder.filename().string();
					if(name.empty())
						name = "NewProject";
					if(Project::Create(folder, name))
						LoadProject(Project::GetProjectFile());
					else
						Toast("Could not create project");
				});
			if(ImGui::MenuItem("Open Project..."))
				m_FileDialog.Open("Open Project", FileDialog::Mode::OpenFile, fs::current_path(), { ".sfproj" }, [this](const fs::path& p) { LoadProject(p); });
			if(ImGui::MenuItem("Save Project", nullptr, false, Project::IsLoaded()))
				Project::Save();
			ImGui::Separator();
			if(ImGui::MenuItem("Export Game...", nullptr, false, Project::IsLoaded()))
				m_FileDialog.Open("Choose Export Folder", FileDialog::Mode::SelectFolder, fs::current_path(), {}, [this](const fs::path& folder) { ExportGame(folder); });
			ImGui::Separator();
			if(ImGui::MenuItem("Exit"))
				Close();
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Edit"))
		{
			std::string undoLabel = "Undo";
			if(m_Ctx.History.CanUndo())
				undoLabel += " " + m_Ctx.History.GetUndoLabel();
			std::string redoLabel = "Redo";
			if(m_Ctx.History.CanRedo())
				redoLabel += " " + m_Ctx.History.GetRedoLabel();
			if(ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_Ctx.History.CanUndo() && !m_Ctx.IsPlaying())) Undo();
			if(ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_Ctx.History.CanRedo() && !m_Ctx.IsPlaying())) Redo();
			ImGui::Separator();
			Entity selected = m_Ctx.GetSelectedEntity();
			if(ImGui::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(selected))) Actions::Duplicate(m_Ctx, selected);
			if(ImGui::MenuItem("Delete", "Del", false, static_cast<bool>(selected))) Actions::Delete(m_Ctx, selected);
			if(ImGui::MenuItem("Focus Selection", "F", false, static_cast<bool>(selected))) FocusSelection(selected);
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Entity"))
		{
			if(ImGui::MenuItem("Create Empty")) Actions::CreateEmpty(m_Ctx);
			if(ImGui::BeginMenu("3D Object"))
			{
				for(const char* name : { "Cube", "Sphere", "Plane", "Quad", "Cylinder", "Capsule", "Cone" })
					if(ImGui::MenuItem(name))
						Actions::CreateMesh(m_Ctx, name, std::string("builtin://") + name);
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu("Light"))
			{
				if(ImGui::MenuItem("Directional Light")) Actions::CreateLight(m_Ctx, LightType::Directional);
				if(ImGui::MenuItem("Point Light")) Actions::CreateLight(m_Ctx, LightType::Point);
				if(ImGui::MenuItem("Spot Light")) Actions::CreateLight(m_Ctx, LightType::Spot);
				ImGui::EndMenu();
			}
			if(ImGui::MenuItem("Camera")) Actions::CreateCamera(m_Ctx);
			if(ImGui::MenuItem("Audio Source")) Actions::CreateAudioSource(m_Ctx);
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
			ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
			ImGui::MenuItem("Content Browser", nullptr, &m_ShowContent);
			ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
			ImGui::MenuItem("Scene Settings / Stats", nullptr, &m_ShowSettings);
			ImGui::Separator();
			ImGui::MenuItem("Grid", nullptr, &m_Ctx.ShowGrid);
			ImGui::MenuItem("Colliders", nullptr, &m_Ctx.ShowColliders);
			ImGui::MenuItem("Light/Camera Gizmos", nullptr, &m_Ctx.ShowLightGizmos);
			ImGui::Separator();
			if(ImGui::MenuItem("Reset Layout"))
				m_RebuildLayout = true;
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Help"))
		{
			ImGui::MenuItem("Keyboard Shortcuts", nullptr, &m_ShowShortcuts);
			ImGui::MenuItem("About Starfall", nullptr, &m_ShowAbout);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	void EditorApp::DrawToolbar()
	{
		auto toggle = [&](const char* label, bool active, const char* tooltip) {
			if(active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			bool pressed = ImGui::Button(label);
			if(active)
				ImGui::PopStyleColor();
			if(ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
			return pressed;
		};

		if(toggle("Move", m_Ctx.Gizmo == GizmoOperation::Translate, "Translate (W)")) m_Ctx.Gizmo = GizmoOperation::Translate;
		ImGui::SameLine();
		if(toggle("Rotate", m_Ctx.Gizmo == GizmoOperation::Rotate, "Rotate (E)")) m_Ctx.Gizmo = GizmoOperation::Rotate;
		ImGui::SameLine();
		if(toggle("Scale", m_Ctx.Gizmo == GizmoOperation::Scale, "Scale (R)")) m_Ctx.Gizmo = GizmoOperation::Scale;
		ImGui::SameLine();
		if(toggle(m_Ctx.GizmoLocal ? "Local" : "World", false, "Gizmo space")) m_Ctx.GizmoLocal = !m_Ctx.GizmoLocal;
		ImGui::SameLine();
		if(toggle("Snap", m_Ctx.Snap, "Snap to grid (hold Ctrl)")) m_Ctx.Snap = !m_Ctx.Snap;

		// Play controls centered
		float center = ImGui::GetWindowWidth() * 0.5f;
		ImGui::SameLine(center - 90);
		if(!m_Ctx.IsPlaying())
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.28f, 1));
			if(ImGui::Button("  Play  ")) Play();
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.22f, 0.22f, 1));
			if(ImGui::Button("  Stop  ")) Stop();
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if(toggle(m_Ctx.Paused ? "Resume" : "Pause", m_Ctx.Paused, "Pause the simulation"))
			{
				m_Ctx.Paused = !m_Ctx.Paused;
				m_Ctx.RuntimeScene->SetPaused(m_Ctx.Paused);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!m_Ctx.Paused);
			if(ImGui::Button("Step"))
				m_Ctx.RuntimeScene->Step(1);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if(toggle(m_Ctx.GameView ? "Game View" : "Scene View", false, "Switch between the game camera and the editor camera"))
				m_Ctx.GameView = !m_Ctx.GameView;
		}
	}

	void EditorApp::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		bool open = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		if(!open)
		{
			m_ViewportHovered = m_ViewportFocused = false;
			ImGui::End();
			return;
		}

		ImGui::SetCursorPos(ImVec2(6, ImGui::GetCursorPosY() + 4));
		DrawToolbar();
		ImGui::NewLine();

		ImVec2 toolbarEnd = ImGui::GetCursorPos();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::SetCursorPos(ImVec2(0, toolbarEnd.y + 4));
		avail = ImGui::GetContentRegionAvail();
		ImVec2 origin = ImGui::GetCursorScreenPos();
		m_ViewportSize = { std::max(avail.x, 16.0f), std::max(avail.y, 16.0f) };
		m_ViewportOrigin = { origin.x, origin.y };

		if(m_ViewportTexture != ImTextureID_Invalid)
			ImGui::Image(ImTextureRef(m_ViewportTexture), ImVec2(m_ViewportSize.x, m_ViewportSize.y));
		else
			ImGui::Dummy(ImVec2(m_ViewportSize.x, m_ViewportSize.y));

		m_ViewportHovered = ImGui::IsItemHovered();
		bool imageClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

		glm::vec2 mouse(ImGui::GetMousePos().x - origin.x, ImGui::GetMousePos().y - origin.y);
		glm::vec2 uv = mouse / m_ViewportSize;
		if(ImGui::BeginDragDropTarget())
		{
			// accept on release
			ImGuiPayload payloadPeek;
			(void)payloadPeek;
			HandleViewportDrop(uv);
			ImGui::EndDragDropTarget();
		}

		bool playingGame = m_Ctx.IsPlaying() && m_Ctx.GameView;
		m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		DrawGizmo(origin, ImVec2(m_ViewportSize.x, m_ViewportSize.y));

		// Click picking: only when the click was not a camera drag and the gizmo is not in use.
		if(imageClicked && !playingGame && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !ImGui::GetIO().KeyAlt)
			m_ClickStart = mouse;
		if(m_ViewportHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !playingGame && !ImGuizmo::IsOver() && !m_GizmoWasUsing && !ImGui::GetIO().KeyAlt)
			if(glm::distance(mouse, m_ClickStart) < 4.0f)
				Pick(uv);

		// Overlay hint
		if(playingGame && !m_ViewportFocused)
		{
			ImGui::SetCursorScreenPos(ImVec2(origin.x + 10, origin.y + 8));
			ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "Click to give the game input focus");
		}
		else if(!playingGame && m_Camera.GetMoveSpeed() > 0.0f && m_ViewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			ImGui::SetCursorScreenPos(ImVec2(origin.x + 10, origin.y + m_ViewportSize.y - 24));
			ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "Fly speed %.1f (scroll to change)", m_Camera.GetMoveSpeed());
		}
		ImGui::End();
	}

	void EditorApp::DrawToasts()
	{
		float y = ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y - 20.0f;
		for(auto it = m_Toasts.begin(); it != m_Toasts.end();)
		{
			it->TimeLeft -= GetDeltaTime();
			if(it->TimeLeft <= 0.0f)
			{
				it = m_Toasts.erase(it);
				continue;
			}
			ImVec2 size = ImGui::CalcTextSize(it->Text.c_str());
			float alpha = std::min(it->TimeLeft, 1.0f);
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			ImVec2 max(ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x - 20.0f, y);
			ImVec2 min(max.x - size.x - 24.0f, max.y - size.y - 14.0f);
			dl->AddRectFilled(min, max, IM_COL32(30, 34, 44, static_cast<int>(235 * alpha)), 6.0f);
			dl->AddRect(min, max, IM_COL32(90, 130, 220, static_cast<int>(255 * alpha)), 6.0f);
			dl->AddText(ImVec2(min.x + 12, min.y + 7), IM_COL32(235, 238, 245, static_cast<int>(255 * alpha)), it->Text.c_str());
			y -= size.y + 22.0f;
			++it;
		}
	}

	void EditorApp::DrawWelcome()
	{
		if(Project::IsLoaded())
			return;
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if(ImGui::Begin("Welcome to Starfall", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
		{
			ImGui::TextUnformatted("Open an existing project or create a new one to get started.");
			ImGui::Spacing();
			if(ImGui::Button("Open Project...", ImVec2(180, 0)))
				m_FileDialog.Open("Open Project", FileDialog::Mode::OpenFile, fs::current_path(), { ".sfproj" }, [this](const fs::path& p) { LoadProject(p); });
			ImGui::SameLine();
			if(ImGui::Button("New Project...", ImVec2(180, 0)))
				m_FileDialog.Open("Choose Folder For New Project", FileDialog::Mode::SelectFolder, fs::current_path(), {}, [this](const fs::path& folder) {
					std::string name = folder.filename().string();
					if(Project::Create(folder, name.empty() ? "NewProject" : name))
						LoadProject(Project::GetProjectFile());
				});
		}
		ImGui::End();
	}

	void EditorApp::OnImGui()
	{
		// Full-window dock host below the main menu bar.
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGuizmo::BeginFrame();
		DrawMenuBar();

		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("DockHost", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar(3);
		ImGuiID dockspace = ImGui::GetID("StarfallDockSpace");
		if(m_RebuildLayout || !ImGui::DockBuilderGetNode(dockspace))
		{
			m_RebuildLayout = false;
			ImGui::DockBuilderRemoveNode(dockspace);
			ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
			ImGuiID center = dockspace;
			ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
			ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
			ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
			ImGui::DockBuilderDockWindow("Hierarchy", left);
			ImGui::DockBuilderDockWindow("Inspector", right);
			ImGui::DockBuilderDockWindow("Scene Settings", right);
			ImGui::DockBuilderDockWindow("Stats", right);
			ImGui::DockBuilderDockWindow("Content Browser", bottom);
			ImGui::DockBuilderDockWindow("Console", bottom);
			ImGui::DockBuilderDockWindow("Viewport", center);
			ImGui::DockBuilderFinish(dockspace);
			m_FocusInspector = 4;
		}
		ImGui::DockSpace(dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::End();

		HandleShortcuts();

		// Startup automation: select an entity / start play after the first frame so panels have valid state.
		if(!m_PendingStartupSelect.empty() && Project::IsLoaded())
		{
			if(Entity e = m_Ctx.GetScene().FindEntityByName(m_PendingStartupSelect))
			{
				m_Ctx.Select(e);
				FocusSelection(e);
			}
			m_PendingStartupSelect.clear();
		}
		if(m_Options.StartPlaying && !m_AutoStartDone && Project::IsLoaded())
		{
			m_AutoStartDone = true;
			Play();
		}

		DrawViewport();
		if(m_ShowHierarchy) m_Hierarchy.OnImGui(m_Ctx, &m_ShowHierarchy);
		if(m_ShowInspector) m_Inspector.OnImGui(m_Ctx, &m_ShowInspector);
		if(m_FocusInspector > 0)
		{
			if(--m_FocusInspector == 0)
				ImGui::SetWindowFocus("Inspector");

		}
		if(m_ShowContent) m_ContentBrowser.OnImGui(m_Ctx, &m_ShowContent);
		if(m_ShowConsole) m_ConsolePanel.OnImGui(m_Ctx, &m_ShowConsole);
		if(m_ShowSettings) m_SettingsPanel.OnImGui(m_Ctx, &m_ShowSettings);

		if(m_ShowShortcuts)
		{
			if(ImGui::Begin("Keyboard Shortcuts", &m_ShowShortcuts, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextUnformatted("Viewport");
				ImGui::BulletText("Right mouse + WASDQE: fly camera (scroll changes speed, Shift = faster)");
				ImGui::BulletText("Middle mouse: pan    Alt + Left mouse: orbit    Scroll: dolly");
				ImGui::BulletText("W / E / R / Q: move / rotate / scale / no gizmo     F: focus selection");
				ImGui::BulletText("Hold Ctrl while dragging a gizmo to snap");
				ImGui::TextUnformatted("Editing");
				ImGui::BulletText("Ctrl+S save    Ctrl+Z undo    Ctrl+Y / Ctrl+Shift+Z redo");
				ImGui::BulletText("Ctrl+D duplicate    Delete remove    F2 rename (hierarchy)");
				ImGui::BulletText("F5 play / stop");
			}
			ImGui::End();
		}
		if(m_ShowAbout)
		{
			if(ImGui::Begin("About Starfall", &m_ShowAbout, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextUnformatted("Starfall Engine");
				ImGui::TextDisabled("Vulkan (nvrhi), Jolt Physics, miniaudio, Lua scripting");
				ImGui::Text("GPU: %s", m_Ctx.AdapterName.c_str());
			}
			ImGui::End();
		}

		DrawWelcome();
		m_FileDialog.Draw();
		DrawToasts();
	}

}
