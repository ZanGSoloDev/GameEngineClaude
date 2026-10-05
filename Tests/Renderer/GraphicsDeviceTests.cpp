#include <doctest/doctest.h>

#include "Starfall/Renderer/GraphicsDevice.h"

#include "GpuTestUtils.h"

using namespace Starfall;

TEST_CASE("GraphicsDevice: headless creation and render target clear/readback")
{
	GpuTestContext* gpu = GetGpuTestContext();
	if(!gpu)
	{
		MESSAGE("no Vulkan device available; skipping GPU tests");
		return;
	}
	CHECK_FALSE(gpu->Device->GetAdapterName().empty());
	CHECK_FALSE(gpu->Device->HasSwapchain());

	nvrhi::IDevice* device = gpu->Device->GetDevice();
	nvrhi::TextureDesc td;
	td.width = 8;
	td.height = 4;
	td.format = nvrhi::Format::RGBA8_UNORM;
	td.isRenderTarget = true;
	td.initialState = nvrhi::ResourceStates::RenderTarget;
	td.keepInitialState = true;
	td.debugName = "TestTarget";
	nvrhi::TextureHandle target = device->createTexture(td);
	REQUIRE(target);

	nvrhi::CommandListHandle cmd = gpu->Device->CreateCommandList();
	cmd->open();
	cmd->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(1.0f, 0.5f, 0.0f, 1.0f));
	cmd->close();
	gpu->Device->ExecuteAndWait(cmd);

	std::vector<uint8_t> pixels = ReadbackTexture(*gpu->Device, target);
	REQUIRE(pixels.size() == 8 * 4 * 4);
	CHECK(pixels[0] == 255);
	CHECK(std::abs(int(pixels[1]) - 128) <= 1);
	CHECK(pixels[2] == 0);
	CHECK(pixels[3] == 255);
}

TEST_CASE("GraphicsDevice: SPIR-V shaders from the build are present and valid")
{
	for(const char* name : { "Test.vert.spv", "Test.frag.spv" })
	{
		auto data = FileSystem::ReadBinary(FileSystem::GetResourcesDirectory() / "Shaders" / name);
		REQUIRE_MESSAGE(data.has_value(), name);
		REQUIRE(data->size() > 20);
		CHECK(data->size() % 4 == 0);
		uint32_t magic;
		std::memcpy(&magic, data->data(), 4);
		CHECK(magic == 0x07230203u); // SPIR-V magic number
	}
}
