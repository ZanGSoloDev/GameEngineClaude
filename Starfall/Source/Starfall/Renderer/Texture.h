#pragma once

#include "Starfall/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Starfall {

	// Decoded 8-bit RGBA image (always 4 channels).
	struct ImageData
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels;
	};

	// Decoded floating point RGBA image (HDR environment maps).
	struct HdrImageData
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<float> Pixels; // RGBA32F
	};

	namespace ImageIO {
		bool DecodeLDR(const void* data, size_t size, ImageData& out);
		bool DecodeHDR(const void* data, size_t size, HdrImageData& out);
		bool LoadLDR(const std::filesystem::path& file, ImageData& out);
		bool LoadHDR(const std::filesystem::path& file, HdrImageData& out);
		bool SavePNG(const std::filesystem::path& file, uint32_t width, uint32_t height, const uint8_t* rgba);
	}

	// 2D texture with a CPU-generated mip chain; uploaded lazily by the renderer.
	class Texture2D
	{
	public:
		static Ref<Texture2D> CreateFromPixels(std::string name, uint32_t width, uint32_t height, std::vector<uint8_t> rgba, bool srgb, bool generateMips = true);
		static Ref<Texture2D> LoadFromMemory(std::string name, const void* data, size_t size, bool srgb);
		static Ref<Texture2D> LoadFromFile(const std::filesystem::path& file, bool srgb);

		const std::string& GetName() const { return m_Name; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetMipCount() const { return m_MipCount; }
		bool IsSRGB() const { return m_SRGB; }
		// Pixel access to mip 0 (valid until the texture is uploaded and the CPU copy released).
		const std::vector<uint8_t>& GetPixels() const { return m_Mips.front(); }
		bool HasCPUData() const { return !m_Mips.empty(); }

		void EnsureGPU(nvrhi::IDevice* device, nvrhi::ICommandList* commandList);
		nvrhi::ITexture* GetTexture() const { return m_Texture; }
		void ReleaseGPU() { m_Texture = nullptr; }

	private:
		Texture2D() = default;

		std::string m_Name;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_MipCount = 1;
		bool m_SRGB = false;
		std::vector<std::vector<uint8_t>> m_Mips;
		nvrhi::TextureHandle m_Texture;
	};

	namespace MipChain {
		// Number of mips for a full chain down to 1x1.
		uint32_t Count(uint32_t width, uint32_t height);
		// Box-filters an RGBA8 image to half size (linear-light averaging for sRGB data; alpha averaged linearly).
		std::vector<uint8_t> Downsample(const std::vector<uint8_t>& src, uint32_t width, uint32_t height, bool srgb);
	}

}
