#include <doctest/doctest.h>

#include "Starfall/Core/Base.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Renderer/ImGuiRenderer.h"
#include "Starfall/Renderer/RenderContext.h"
#include "Starfall/Renderer/SceneRenderer.h"
#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneSerializer.h"

#include "GpuTestUtils.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <cstdlib>

using namespace Starfall;

namespace {

	// Captures error logs while alive so tests can assert the GPU validation layer stayed silent.
	struct ErrorCapture
	{
		ErrorCapture()
		{
			m_Handle = Log::AddSink([this](LogLevel level, std::string_view, std::string_view message) {
				if(level >= LogLevel::Error)
					Errors.emplace_back(message);
			});
		}
		~ErrorCapture() { Log::RemoveSink(m_Handle); }
		std::vector<std::string> Errors;

	private:
		uint32_t m_Handle;
	};

	RenderCamera Camera(uint32_t w, uint32_t h, glm::vec3 eye, glm::vec3 target)
	{
		RenderCamera cam;
		cam.Position = eye;
		cam.View = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
		cam.Projection = glm::perspective(glm::radians(50.0f), float(w) / float(h), 0.1f, 200.0f);
		return cam;
	}

	double MeanAbsDiff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
	{
		double sum = 0;
		size_t n = std::min(a.size(), b.size());
		for(size_t i = 0; i < n; i++)
			sum += std::abs(int(a[i]) - int(b[i]));
		return sum / double(n);
	}

	std::vector<uint8_t> RenderToPixels(GpuTestContext& gpu, SceneRenderer& renderer, Scene& scene, const RenderCamera& cam, uint32_t w, uint32_t h, const SceneRenderOptions& options)
	{
		nvrhi::CommandListHandle cmd = gpu.Device->CreateCommandList();
		cmd->open();
		renderer.Render(cmd, scene, cam, w, h, options);
		cmd->close();
		gpu.Device->ExecuteAndWait(cmd);
		return ReadbackTexture(*gpu.Device, renderer.GetOutput());
	}

	void SimpleScene(Scene& scene)
	{
		Entity floor = scene.CreateEntity("Floor");
		floor.Transform().Scale = { 20, 1, 20 };
		floor.AddComponent<MeshRendererComponent>().Mesh = "builtin://Plane";
		Entity sphere = scene.CreateEntity("Sphere");
		sphere.Transform().Translation = { 0, 0.5f, 0 };
		sphere.AddComponent<MeshRendererComponent>().Mesh = "builtin://Sphere";
		Entity sun = scene.CreateEntity("Sun");
		sun.Transform().Rotation = { glm::radians(-50.0f), glm::radians(30.0f), 0 };
		auto& l = sun.AddComponent<LightComponent>();
		l.Type = LightType::Directional;
		l.Intensity = 3.0f;
	}

}

TEST_CASE("SceneRenderer renders the whole feature test project without errors")
{
	GpuTestContext* gpu = GetGpuTestContext();
	if(!gpu)
		return;
	ErrorCapture errors;
	REQUIRE(Project::Load(std::filesystem::path(STARFALL_TEST_PROJECT_DIR) / "TestProject.sfproj"));
	AssetManager::Clear();
	Scope<RenderContext> context = RenderContext::Create(*gpu->Device);
	REQUIRE(context);
	{
		Scene scene;
		REQUIRE(SceneSerializer::Deserialize(scene, Project::ResolvePath(Project::GetStartScene())));
		SceneRenderer renderer(*context);
		SceneRenderOptions options;
		const uint32_t W = 800, H = 450;
		RenderCamera cam = Camera(W, H, { 0, 5, 12 }, { 0, 1, 0 });
		auto pixels = RenderToPixels(*gpu, renderer, scene, cam, W, H, options);

		const RenderStats& s = renderer.GetStats();
		CHECK(s.DrawCalls >= 18);
		CHECK(s.LocalLights == 2);
		CHECK(s.ShadowedLocalLights == 2);
		CHECK(s.Cascades == 4);
		CHECK(s.Triangles > 10000);
		if(std::getenv("SF_TEST_SAVE_IMAGES"))
			ImageIO::SavePNG("render_project.png", W, H, pixels.data());

		SceneRenderOptions noShadows = options;
		noShadows.EnableShadows = false;
		CHECK(MeanAbsDiff(pixels, RenderToPixels(*gpu, renderer, scene, cam, W, H, noShadows)) > 0.2);
		SceneRenderOptions noAo = options;
		noAo.EnableSSAO = false;
		CHECK(MeanAbsDiff(pixels, RenderToPixels(*gpu, renderer, scene, cam, W, H, noAo)) > 0.02);

		std::vector<std::vector<uint8_t>> modes;
		for(Tonemapper mode : { Tonemapper::None, Tonemapper::Reinhard, Tonemapper::ACES, Tonemapper::AgX })
		{
			scene.GetSettings().PostProcess.TonemapMode = mode;
			modes.push_back(RenderToPixels(*gpu, renderer, scene, cam, W, H, options));
		}
		for(size_t i = 0; i < modes.size(); i++)
			for(size_t j = i + 1; j < modes.size(); j++)
				CHECK_MESSAGE(MeanAbsDiff(modes[i], modes[j]) > 0.5, "tonemappers " << i << " vs " << j);

		CHECK(RenderToPixels(*gpu, renderer, scene, Camera(320, 200, { 0, 5, 12 }, { 0, 1, 0 }), 320, 200, options).size() == 320u * 200u * 4u);
		CHECK(RenderToPixels(*gpu, renderer, scene, Camera(1024, 512, { 0, 5, 12 }, { 0, 1, 0 }), 1024, 512, options).size() == 1024u * 512u * 4u);

		scene.GetSettings().Environment.HDRI.clear(); // procedural sky
		CHECK(MeanAbsDiff(RenderToPixels(*gpu, renderer, scene, cam, W, H, options), modes.back()) > 0.1);

		Scene lightless;
		lightless.CreateEntity("Cube").AddComponent<MeshRendererComponent>();
		CHECK(RenderToPixels(*gpu, renderer, lightless, cam, W, H, options).size() == size_t(W) * H * 4);
		Scene empty;
		RenderToPixels(*gpu, renderer, empty, cam, W, H, options);
	}
	context.reset();
	AssetManager::Clear();
	Project::Unload();
	for(const auto& e : errors.Errors)
		FAIL_CHECK("render error logged: " << e);
}

TEST_CASE("SceneRenderer overlays: grid, debug lines and selection outline")
{
	GpuTestContext* gpu = GetGpuTestContext();
	if(!gpu)
		return;
	ErrorCapture errors;
	Scope<RenderContext> context = RenderContext::Create(*gpu->Device);
	REQUIRE(context);
	Scene scene;
	SimpleScene(scene);
	SceneRenderer renderer(*context);
	const uint32_t W = 480, H = 270;
	RenderCamera cam = Camera(W, H, { 0, 2.5f, 6 }, { 0, 0.4f, 0 });
	SceneRenderOptions base;
	auto plain = RenderToPixels(*gpu, renderer, scene, cam, W, H, base);

	SceneRenderOptions grid = base;
	grid.DrawGrid = true;
	CHECK(MeanAbsDiff(plain, RenderToPixels(*gpu, renderer, scene, cam, W, H, grid)) > 0.05);

	renderer.GetDebugRenderer().DrawSphere({ 0, 1, 0 }, 1.0f, { 1, 0, 0, 1 }, true);
	renderer.GetDebugRenderer().DrawBox(glm::mat4(1.0f), { 1, 1, 1 }, { 0, 1, 0, 1 }, false);
	renderer.GetDebugRenderer().DrawFrustum(cam.Projection * glm::lookAt(glm::vec3(5, 3, 5), glm::vec3(0), glm::vec3(0, 1, 0)), { 0, 0, 1, 1 });
	CHECK(renderer.GetDebugRenderer().GetLineCount() > 50);
	CHECK(MeanAbsDiff(plain, RenderToPixels(*gpu, renderer, scene, cam, W, H, base)) > 0.1);
	CHECK(renderer.GetDebugRenderer().GetLineCount() == 0); // consumed by the frame

	SceneRenderOptions outlined = base;
	outlined.OutlinedEntities.push_back(scene.FindEntityByName("Sphere").GetUUID());
	CHECK(MeanAbsDiff(plain, RenderToPixels(*gpu, renderer, scene, cam, W, H, outlined)) > 0.02);

	for(const auto& e : errors.Errors)
		FAIL_CHECK("render error logged: " << e);
}

TEST_CASE("Point and spot light shadows change the lit region")
{
	GpuTestContext* gpu = GetGpuTestContext();
	if(!gpu)
		return;
	ErrorCapture errors;
	Scope<RenderContext> context = RenderContext::Create(*gpu->Device);
	REQUIRE(context);
	Scene scene;
	scene.GetSettings().Environment.Intensity = 0.0f;
	Entity floor = scene.CreateEntity("Floor");
	floor.Transform().Scale = { 20, 1, 20 };
	floor.AddComponent<MeshRendererComponent>().Mesh = "builtin://Plane";
	Entity blocker = scene.CreateEntity("Blocker");
	blocker.Transform().Translation = { 0, 1.5f, 0 };
	blocker.AddComponent<MeshRendererComponent>().Mesh = "builtin://Cube";
	Entity light = scene.CreateEntity("Light");
	light.Transform().Translation = { 0, 4, 0 };
	auto& l = light.AddComponent<LightComponent>();
	l.Type = LightType::Point;
	l.Intensity = 200.0f;
	l.Range = 20.0f;
	l.CastShadows = true;

	SceneRenderer renderer(*context);
	SceneRenderOptions options;
	options.EnableSSAO = false;
	const uint32_t W = 400, H = 300;
	RenderCamera cam = Camera(W, H, { 0, 9, 0.01f }, { 0, 0, 0 });
	auto shadowed = RenderToPixels(*gpu, renderer, scene, cam, W, H, options);
	CHECK(renderer.GetStats().ShadowedLocalLights == 1);
	l.CastShadows = false;
	auto unshadowed = RenderToPixels(*gpu, renderer, scene, cam, W, H, options);
	CHECK(MeanAbsDiff(shadowed, unshadowed) > 0.3);

	l.Type = LightType::Spot;
	l.CastShadows = true;
	light.Transform().Rotation = { glm::radians(-90.0f), 0, 0 };
	auto spotShadowed = RenderToPixels(*gpu, renderer, scene, cam, W, H, options);
	CHECK(renderer.GetStats().ShadowedLocalLights == 1);
	l.CastShadows = false;
	auto spotUnshadowed = RenderToPixels(*gpu, renderer, scene, cam, W, H, options);
	CHECK(MeanAbsDiff(spotShadowed, spotUnshadowed) > 0.3);

	for(const auto& e : errors.Errors)
		FAIL_CHECK("render error logged: " << e);
}

TEST_CASE("ImGuiRenderer draws UI into a texture")
{
	GpuTestContext* gpu = GetGpuTestContext();
	if(!gpu)
		return;
	ErrorCapture errors;
	Scope<RenderContext> context = RenderContext::Create(*gpu->Device);
	REQUIRE(context);

	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().DisplaySize = ImVec2(320, 200);
	std::vector<uint8_t> pixels;
	{
		ImGuiRenderer imgui(*context, nullptr);
		nvrhi::TextureDesc td;
		td.width = 320;
		td.height = 200;
		td.format = nvrhi::Format::BGRA8_UNORM;
		td.isRenderTarget = true;
		td.initialState = nvrhi::ResourceStates::ShaderResource;
		td.keepInitialState = true;
		nvrhi::TextureHandle target = gpu->Device->GetDevice()->createTexture(td);
		nvrhi::FramebufferHandle fb = gpu->Device->GetDevice()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));

		for(int frame = 0; frame < 3; frame++) // the font atlas is created on the first frames
		{
			imgui.BeginFrame();
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(10, 10));
			ImGui::SetNextWindowSize(ImVec2(200, 120));
			ImGui::Begin("Test");
			ImGui::Text("Hello Starfall");
			ImGui::Button("Button");
			ImGui::End();
			ImGui::Render();

			nvrhi::CommandListHandle cmd = gpu->Device->CreateCommandList();
			cmd->open();
			cmd->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 1));
			imgui.Render(cmd, fb);
			cmd->close();
			gpu->Device->ExecuteAndWait(cmd);
		}
		pixels = ReadbackTexture(*gpu->Device, target);
	}
	ImGui::DestroyContext();
	uint32_t lit = 0;
	for(size_t i = 0; i < pixels.size(); i += 4)
		if(pixels[i] + pixels[i + 1] + pixels[i + 2] > 60)
			lit++;
	CHECK(lit > 500); // window background and text were drawn
	context.reset();
	for(const auto& e : errors.Errors)
		FAIL_CHECK("render error logged: " << e);
}
