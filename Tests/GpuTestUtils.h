#pragma once

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Renderer/GraphicsDevice.h"

#include <cstring>
#include <vector>

// One headless Vulkan device shared by all GPU tests (device creation is slow). Null when no GPU is available.
struct GpuTestContext
{
	Starfall::Scope<Starfall::GraphicsDevice> Device;
};

GpuTestContext* GetGpuTestContext();
void ShutdownGpuTestContext();

// Copies a texture (mip 0, layer 0) back to the CPU. Returns tightly packed rows.
std::vector<uint8_t> ReadbackTexture(Starfall::GraphicsDevice& device, nvrhi::ITexture* texture);
