#include <doctest/doctest.h>

#include "Starfall/Renderer/RenderContext.h"
#include "Starfall/Renderer/SceneRenderer.h"
#include "Starfall/Scene/Entity.h"

#include "GpuTestUtils.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>

using namespace Starfall;

namespace {

	RenderCamera MakeCamera(uint32_t w, uint32_t h, glm::vec3 eye, glm::vec3 target)
	{
		RenderCamera cam;
		cam.Position = eye;
		cam.View = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
		cam.Projection = glm::perspective(glm::radians(50.0f), float(w) / float(h), 0.1f, 200.0f);
		return cam;
	}

	void BuildTestScene(Scene& scene)
	{
		Entity floor = scene.CreateEntity("Floor");
		floor.Transform().Scale = { 20, 1, 20 };
		floor.AddComponent<MeshRendererComponent>().Mesh = "builtin://Plane";

		Entity cube = scene.CreateEntity("Cube");
		cube.Transform().Translation = { -1.2f, 0.5f, 0 };
		cube.AddComponent<MeshRendererComponent>().Mesh = "builtin://Cube";

		Entity sphere = scene.CreateEntity("Sphere");
		sphere.Transform().Translation = { 1.2f, 0.5f, 0 };
		sphere.AddComponent<MeshRendererComponent>().Mesh = "builtin://Sphere";

		Entity sun = scene.CreateEntity("Sun");
		sun.Transform().Rotation = { glm::radians(-50.0f), glm::radians(30.0f), 0 };
		auto& l = sun.AddComponent<LightComponent>();
		l.Type = LightType::Directional;
		l.Intensity = 3.0f;
	}

}

TEST_CASE("SceneRenderer renders a lit scene with shadows, SSAO and tonemapping")
{
	GpuTestContext* gpu = GetGpuTestContext();
	if(!gpu)
	{
		MESSAGE("no Vulkan device; skipping");
		return;
	}
	Scope<RenderContext> context = RenderContext::Create(*gpu->Device);
	REQUIRE(context);

	Scene scene;
	BuildTestScene(scene);
	const uint32_t W = 640, H = 360;
	SceneRenderer renderer(*context);
	SceneRenderOptions options;

	nvrhi::CommandListHandle cmd = gpu->Device->CreateCommandList();
	cmd->open();
	renderer.Render(cmd, scene, MakeCamera(W, H, { 0, 2.5f, 6 }, { 0, 0.4f, 0 }), W, H, options);
	cmd->close();
	gpu->Device->ExecuteAndWait(cmd);

	const RenderStats& stats = renderer.GetStats();
	CHECK(stats.DrawCalls == 3);
	CHECK(stats.Cascades > 0);
	CHECK(stats.ShadowDrawCalls > 0);

	std::vector<uint8_t> pixels = ReadbackTexture(*gpu->Device, renderer.GetOutput());
	REQUIRE(pixels.size() == size_t(W) * H * 4);

	uint64_t sum = 0;
	uint32_t distinct = 0;
	uint32_t prev = ~0u;
	for(size_t i = 0; i < pixels.size(); i += 4)
	{
		sum += pixels[i] + pixels[i + 1] + pixels[i + 2];
		uint32_t px = pixels[i] | pixels[i + 1] << 8 | pixels[i + 2] << 16;
		if(px != prev)
			distinct++;
		prev = px;
	}
	CHECK(sum > 0);
	CHECK(distinct > 1000); // not a flat color

	if(std::getenv("SF_TEST_SAVE_IMAGES"))
		ImageIO::SavePNG("render_test.png", W, H, pixels.data());
}
