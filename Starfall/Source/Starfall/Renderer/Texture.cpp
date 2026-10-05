#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "Starfall/Renderer/Texture.h"

#include "Starfall/Core/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Starfall {

	namespace {

		constexpr uint32_t MaxTextureDimension = 16384;

		const std::array<float, 256>& SRGBToLinearTable()
		{
			static const std::array<float, 256> table = [] {
				std::array<float, 256> t{};
				for(int i = 0; i < 256; i++)
				{
					float c = static_cast<float>(i) / 255.0f;
					t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
				return t;
			}();
			return table;
		}

		uint8_t LinearToSRGB8(float c)
		{
			c = std::clamp(c, 0.0f, 1.0f);
			float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::lround(s * 255.0f));
		}

	}

	namespace ImageIO {

		bool DecodeLDR(const void* data, size_t size, ImageData& out)
		{
			if(!data || size == 0 || size > static_cast<size_t>(INT32_MAX))
				return false;
			int w = 0, h = 0, channels = 0;
			stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &channels, 4);
			if(!pixels)
			{
				SF_CORE_WARN("Image decode failed: {0}", stbi_failure_reason());
				return false;
			}
			if(w <= 0 || h <= 0 || static_cast<uint32_t>(w) > MaxTextureDimension || static_cast<uint32_t>(h) > MaxTextureDimension)
			{
				stbi_image_free(pixels);
				return false;
			}
			out.Width = static_cast<uint32_t>(w);
			out.Height = static_cast<uint32_t>(h);
			out.Pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
			stbi_image_free(pixels);
			return true;
		}

		bool DecodeHDR(const void* data, size_t size, HdrImageData& out)
		{
			if(!data || size == 0 || size > static_cast<size_t>(INT32_MAX))
				return false;
			int w = 0, h = 0, channels = 0;
			float* pixels = stbi_loadf_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &channels, 4);
			if(!pixels)
			{
				SF_CORE_WARN("HDR decode failed: {0}", stbi_failure_reason());
				return false;
			}
			if(w <= 0 || h <= 0 || static_cast<uint32_t>(w) > MaxTextureDimension || static_cast<uint32_t>(h) > MaxTextureDimension)
			{
				stbi_image_free(pixels);
				return false;
			}
			out.Width = static_cast<uint32_t>(w);
			out.Height = static_cast<uint32_t>(h);
			out.Pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
			stbi_image_free(pixels);
			return true;
		}

		bool LoadLDR(const std::filesystem::path& file, ImageData& out)
		{
			auto bytes = FileSystem::ReadBinary(file);
			return bytes && DecodeLDR(bytes->data(), bytes->size(), out);
		}

		bool LoadHDR(const std::filesystem::path& file, HdrImageData& out)
		{
			auto bytes = FileSystem::ReadBinary(file);
			return bytes && DecodeHDR(bytes->data(), bytes->size(), out);
		}

		bool SavePNG(const std::filesystem::path& file, uint32_t width, uint32_t height, const uint8_t* rgba)
		{
			int length = 0;
			unsigned char* png = stbi_write_png_to_mem(rgba, static_cast<int>(width * 4), static_cast<int>(width), static_cast<int>(height), 4, &length);
			if(!png)
				return false;
			bool ok = FileSystem::WriteBinary(file, png, static_cast<size_t>(length));
			STBIW_FREE(png);
			return ok;
		}

	}

	namespace MipChain {

		uint32_t Count(uint32_t width, uint32_t height)
		{
			uint32_t levels = 1;
			uint32_t size = std::max(width, height);
			while(size > 1)
			{
				size >>= 1;
				levels++;
			}
			return levels;
		}

		std::vector<uint8_t> Downsample(const std::vector<uint8_t>& src, uint32_t width, uint32_t height, bool srgb)
		{
			uint32_t nw = std::max(width / 2, 1u);
			uint32_t nh = std::max(height / 2, 1u);
			std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
			const auto& lut = SRGBToLinearTable();
			for(uint32_t y = 0; y < nh; y++)
			{
				for(uint32_t x = 0; x < nw; x++)
				{
					float sum[4] = { 0, 0, 0, 0 };
					int count = 0;
					for(uint32_t oy = 0; oy < 2; oy++)
					{
						for(uint32_t ox = 0; ox < 2; ox++)
						{
							uint32_t sx = std::min(x * 2 + ox, width - 1);
							uint32_t sy = std::min(y * 2 + oy, height - 1);
							const uint8_t* p = &src[(static_cast<size_t>(sy) * width + sx) * 4];
							for(int c = 0; c < 3; c++)
								sum[c] += srgb ? lut[p[c]] : static_cast<float>(p[c]) / 255.0f;
							sum[3] += static_cast<float>(p[3]) / 255.0f;
							count++;
						}
					}
					uint8_t* out = &dst[(static_cast<size_t>(y) * nw + x) * 4];
					for(int c = 0; c < 3; c++)
					{
						float v = sum[c] / count;
						out[c] = srgb ? LinearToSRGB8(v) : static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
					}
					out[3] = static_cast<uint8_t>(std::lround(std::clamp(sum[3] / count, 0.0f, 1.0f) * 255.0f));
				}
			}
			return dst;
		}

	}

	Ref<Texture2D> Texture2D::CreateFromPixels(std::string name, uint32_t width, uint32_t height, std::vector<uint8_t> rgba, bool srgb, bool generateMips)
	{
		if(width == 0 || height == 0 || rgba.size() != static_cast<size_t>(width) * height * 4)
		{
			SF_CORE_ERROR("Invalid texture data for '{0}'", name);
			return nullptr;
		}
		Ref<Texture2D> texture(new Texture2D());
		texture->m_Name = std::move(name);
		texture->m_Width = width;
		texture->m_Height = height;
		texture->m_SRGB = srgb;
		texture->m_Mips.push_back(std::move(rgba));
		if(generateMips)
		{
			uint32_t w = width, h = height;
			while(w > 1 || h > 1)
			{
				texture->m_Mips.push_back(MipChain::Downsample(texture->m_Mips.back(), w, h, srgb));
				w = std::max(w / 2, 1u);
				h = std::max(h / 2, 1u);
			}
		}
		texture->m_MipCount = static_cast<uint32_t>(texture->m_Mips.size());
		return texture;
	}

	Ref<Texture2D> Texture2D::LoadFromMemory(std::string name, const void* data, size_t size, bool srgb)
	{
		ImageData image;
		if(!ImageIO::DecodeLDR(data, size, image))
			return nullptr;
		return CreateFromPixels(std::move(name), image.Width, image.Height, std::move(image.Pixels), srgb);
	}

	Ref<Texture2D> Texture2D::LoadFromFile(const std::filesystem::path& file, bool srgb)
	{
		ImageData image;
		if(!ImageIO::LoadLDR(file, image))
		{
			SF_CORE_WARN("Could not load texture '{0}'", file.string());
			return nullptr;
		}
		return CreateFromPixels(file.filename().string(), image.Width, image.Height, std::move(image.Pixels), srgb);
	}

	void Texture2D::EnsureGPU(nvrhi::IDevice* device, nvrhi::ICommandList* commandList)
	{
		if(m_Texture || m_Mips.empty())
			return;

		nvrhi::TextureDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.mipLevels = m_MipCount;
		desc.format = m_SRGB ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		desc.debugName = m_Name;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		m_Texture = device->createTexture(desc);

		uint32_t w = m_Width, h = m_Height;
		for(uint32_t mip = 0; mip < m_MipCount; mip++)
		{
			commandList->writeTexture(m_Texture, 0, mip, m_Mips[mip].data(), static_cast<size_t>(w) * 4);
			w = std::max(w / 2, 1u);
			h = std::max(h / 2, 1u);
		}
		// Small textures (defaults, UI) keep their CPU copy so they can be re-uploaded; large ones free it.
		size_t total = 0;
		for(const auto& mip : m_Mips)
			total += mip.size();
		if(total > (1u << 20))
		{
			m_Mips.clear();
			m_Mips.shrink_to_fit();
		}
	}

}
