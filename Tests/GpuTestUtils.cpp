#include "GpuTestUtils.h"

#include "Starfall/Platform/Window.h"

#include <cstdlib>

using namespace Starfall;

namespace {
	bool s_Attempted = false;
	GpuTestContext s_Context;
	bool s_Available = false;
}

GpuTestContext* GetGpuTestContext()
{
	if(!s_Attempted)
	{
		s_Attempted = true;
		if(std::getenv("SF_TEST_NO_GPU"))
			return nullptr;
		GraphicsDeviceDesc desc;
		desc.EnableValidation = true;
		s_Context.Device = GraphicsDevice::Create(desc);
		s_Available = s_Context.Device != nullptr;
	}
	return s_Available ? &s_Context : nullptr;
}

void ShutdownGpuTestContext()
{
	if(s_Context.Device)
		s_Context.Device->WaitIdle();
	s_Context.Device.reset();
	s_Available = false;
}

std::vector<uint8_t> ReadbackTexture(GraphicsDevice& device, nvrhi::ITexture* texture)
{
	const nvrhi::TextureDesc& td = texture->getDesc();
	nvrhi::StagingTextureHandle staging;
	{
		nvrhi::TextureDesc sd = td;
		sd.isRenderTarget = false;
		sd.isUAV = false;
		sd.initialState = nvrhi::ResourceStates::CopyDest;
		sd.keepInitialState = true;
		sd.mipLevels = 1;
		sd.arraySize = 1;
		sd.sampleCount = 1;
		staging = device.GetDevice()->createStagingTexture(sd, nvrhi::CpuAccessMode::Read);
	}

	nvrhi::CommandListHandle cmd = device.CreateCommandList();
	cmd->open();
	cmd->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice().setMipLevel(0));
	cmd->close();
	device.ExecuteAndWait(cmd);

	size_t pixelSize = nvrhi::getFormatInfo(td.format).bytesPerBlock;
	size_t rowBytes = td.width * pixelSize;
	std::vector<uint8_t> result(rowBytes * td.height);
	size_t pitch = 0;
	const uint8_t* mapped = static_cast<const uint8_t*>(device.GetDevice()->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
	for(uint32_t y = 0; y < td.height; y++)
		std::memcpy(result.data() + y * rowBytes, mapped + y * pitch, rowBytes);
	device.GetDevice()->unmapStagingTexture(staging);
	return result;
}
