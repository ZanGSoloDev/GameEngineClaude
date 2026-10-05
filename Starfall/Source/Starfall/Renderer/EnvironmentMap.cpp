#include "Starfall/Renderer/EnvironmentMap.h"

#include "Starfall/Renderer/AssetManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Starfall {

	uint16_t FloatToHalf(float value)
	{
		uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		uint32_t sign = (bits >> 16) & 0x8000u;
		int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
		uint32_t mantissa = bits & 0x7FFFFFu;

		if(((bits >> 23) & 0xFF) == 0xFF) // inf / nan
			return static_cast<uint16_t>(sign | (mantissa ? 0x7E00u : 0x7BFFu)); // nan -> qNaN, inf -> max finite
		if(exponent >= 31)
			return static_cast<uint16_t>(sign | 0x7BFFu); // overflow saturates
		if(exponent <= 0)
		{
			if(exponent < -10)
				return static_cast<uint16_t>(sign); // underflow to zero
			mantissa |= 0x800000u;
			uint32_t shift = static_cast<uint32_t>(14 - exponent);
			uint32_t half = mantissa >> shift;
			uint32_t remainder = mantissa & ((1u << shift) - 1);
			uint32_t halfway = 1u << (shift - 1);
			if(remainder > halfway || (remainder == halfway && (half & 1)))
				half++;
			return static_cast<uint16_t>(sign | half);
		}
		uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
		uint32_t remainder = mantissa & 0x1FFFu;
		if(remainder > 0x1000u || (remainder == 0x1000u && (half & 1)))
			half++; // may carry into the exponent, which is the correct rounding behaviour
		return static_cast<uint16_t>(half);
	}

	namespace {

		nvrhi::TextureHandle CreateCube(nvrhi::IDevice* device, uint32_t size, uint32_t mips, const char* name)
		{
			nvrhi::TextureDesc desc;
			desc.width = desc.height = size;
			desc.arraySize = 6;
			desc.mipLevels = mips;
			desc.dimension = nvrhi::TextureDimension::TextureCube;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.isUAV = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			desc.debugName = name;
			return device->createTexture(desc);
		}

		nvrhi::BindingSetItem CubeUAV(uint32_t slot, nvrhi::ITexture* texture, uint32_t mip)
		{
			return nvrhi::BindingSetItem::Texture_UAV(slot, texture, nvrhi::Format::RGBA16_FLOAT, nvrhi::TextureSubresourceSet(mip, 1, 0, 6), nvrhi::TextureDimension::Texture2DArray);
		}

		uint32_t Groups(uint32_t size) { return (size + 7) / 8; }

		// Box-filters an RGBA32F image to half size.
		std::vector<float> DownsampleFloat(const std::vector<float>& src, uint32_t width, uint32_t height)
		{
			uint32_t nw = std::max(width / 2, 1u);
			uint32_t nh = std::max(height / 2, 1u);
			std::vector<float> dst(static_cast<size_t>(nw) * nh * 4);
			for(uint32_t y = 0; y < nh; y++)
			{
				for(uint32_t x = 0; x < nw; x++)
				{
					float sum[4] = { 0, 0, 0, 0 };
					for(uint32_t oy = 0; oy < 2; oy++)
						for(uint32_t ox = 0; ox < 2; ox++)
						{
							uint32_t sx = std::min(x * 2 + ox, width - 1);
							uint32_t sy = std::min(y * 2 + oy, height - 1);
							const float* p = &src[(static_cast<size_t>(sy) * width + sx) * 4];
							for(int c = 0; c < 4; c++)
								sum[c] += p[c];
						}
					for(int c = 0; c < 4; c++)
						dst[(static_cast<size_t>(y) * nw + x) * 4 + c] = sum[c] * 0.25f;
				}
			}
			return dst;
		}

	}

	EnvironmentMap::EnvironmentMap(RenderContext& context)
		: m_Context(context)
	{
		nvrhi::IDevice* device = context.GetDevice();
		m_EnvironmentMips = MipChain::Count(EnvironmentSize, EnvironmentSize);
		m_PrefilterMips = MipChain::Count(PrefilterSize, PrefilterSize);
		m_Environment = CreateCube(device, EnvironmentSize, m_EnvironmentMips, "Environment Cube");
		m_Irradiance = CreateCube(device, IrradianceSize, 1, "Irradiance Cube");
		m_Prefiltered = CreateCube(device, PrefilterSize, m_PrefilterMips, "Prefiltered Cube");

		nvrhi::BindingLayoutDesc equirect;
		equirect.setVisibility(nvrhi::ShaderType::Compute).setBindingOffsets(ZeroBindingOffsets());
		equirect.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		equirect.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		equirect.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2));
		equirect.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::vec4)));
		m_EquirectLayout = device->createBindingLayout(equirect);

		nvrhi::BindingLayoutDesc gradient;
		gradient.setVisibility(nvrhi::ShaderType::Compute).setBindingOffsets(ZeroBindingOffsets());
		gradient.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2));
		gradient.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::vec4) * 3));
		m_GradientLayout = device->createBindingLayout(gradient);

		m_FilterLayout = m_EquirectLayout; // same shape: SRV cube + sampler + UAV + vec4 push constants

		m_EquirectPipeline = context.CreateComputePipeline("equirect_to_cube.comp", m_EquirectLayout);
		m_GradientPipeline = context.CreateComputePipeline("gradient_to_cube.comp", m_GradientLayout);
		m_IrradiancePipeline = context.CreateComputePipeline("irradiance.comp", m_FilterLayout);
		m_PrefilterPipeline = context.CreateComputePipeline("prefilter.comp", m_FilterLayout);
	}

	bool EnvironmentMap::Update(nvrhi::ICommandList* commandList, const EnvironmentSettings& settings)
	{
		if(!m_EquirectPipeline || !m_GradientPipeline || !m_IrradiancePipeline || !m_PrefilterPipeline)
			return false;

		std::string key = settings.HDRI.empty()
			? std::format("sky:{:.3f},{:.3f},{:.3f}|{:.3f},{:.3f},{:.3f}", settings.SkyColor.r, settings.SkyColor.g, settings.SkyColor.b, settings.GroundColor.r, settings.GroundColor.g, settings.GroundColor.b)
			: "hdri:" + settings.HDRI;
		if(key == m_Key)
			return false;

		Ref<HdrImageData> hdri = settings.HDRI.empty() ? nullptr : AssetManager::GetHDRI(settings.HDRI);
		if(hdri && hdri->Width >= 2 && hdri->Height >= 2)
		{
			BuildFromEquirect(commandList, *hdri);
		}
		else
		{
			if(!settings.HDRI.empty())
				key = "sky-fallback:" + settings.HDRI; // failed HDRI load: show the procedural sky and do not retry every frame
			BuildProcedural(commandList, settings);
		}
		FilterEnvironment(commandList);
		m_Key = key;
		return true;
	}

	void EnvironmentMap::BuildFromEquirect(nvrhi::ICommandList* commandList, const HdrImageData& image)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();

		// CPU mip chain in float, capped at 4096 wide to bound memory.
		std::vector<std::vector<float>> levels;
		std::vector<std::pair<uint32_t, uint32_t>> sizes;
		levels.push_back(image.Pixels);
		sizes.emplace_back(image.Width, image.Height);
		while(sizes.back().first > 4096)
		{
			levels.back() = DownsampleFloat(levels.back(), sizes.back().first, sizes.back().second);
			sizes.back() = { std::max(sizes.back().first / 2, 1u), std::max(sizes.back().second / 2, 1u) };
		}
		uint32_t mipCount = MipChain::Count(sizes.back().first, sizes.back().second);
		while(levels.size() < mipCount)
		{
			auto [w, h] = sizes.back();
			levels.push_back(DownsampleFloat(levels.back(), w, h));
			sizes.emplace_back(std::max(w / 2, 1u), std::max(h / 2, 1u));
		}

		nvrhi::TextureDesc desc;
		desc.width = sizes[0].first;
		desc.height = sizes[0].second;
		desc.mipLevels = mipCount;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = "HDRI Equirect";
		nvrhi::TextureHandle equirect = device->createTexture(desc);

		for(uint32_t mip = 0; mip < mipCount; mip++)
		{
			std::vector<uint16_t> half(levels[mip].size());
			for(size_t i = 0; i < half.size(); i++)
			{
				float v = levels[mip][i];
				half[i] = FloatToHalf((v == v) ? std::clamp(v, 0.0f, 60000.0f) : 0.0f);
			}
			commandList->writeTexture(equirect, 0, mip, half.data(), static_cast<size_t>(sizes[mip].first) * 8);
		}

		for(uint32_t mip = 0; mip < m_EnvironmentMips; mip++)
		{
			uint32_t size = std::max(EnvironmentSize >> mip, 1u);
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, equirect));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Context.GetEquirectSampler()));
			set.addItem(CubeUAV(2, m_Environment, mip));
			set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::vec4)));
			nvrhi::BindingSetHandle bindings = device->createBindingSet(set, m_EquirectLayout);

			nvrhi::ComputeState state;
			state.setPipeline(m_EquirectPipeline);
			state.addBindingSet(bindings);
			commandList->setComputeState(state);
			glm::vec4 params(static_cast<float>(size), 0.0f, 0.0f, 0.0f);
			commandList->setPushConstants(&params, sizeof(params));
			commandList->dispatch(Groups(size), Groups(size), 6);
		}
	}

	void EnvironmentMap::BuildProcedural(nvrhi::ICommandList* commandList, const EnvironmentSettings& settings)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();
		for(uint32_t mip = 0; mip < m_EnvironmentMips; mip++)
		{
			uint32_t size = std::max(EnvironmentSize >> mip, 1u);
			nvrhi::BindingSetDesc set;
			set.addItem(CubeUAV(2, m_Environment, mip));
			set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::vec4) * 3));
			nvrhi::BindingSetHandle bindings = device->createBindingSet(set, m_GradientLayout);

			nvrhi::ComputeState state;
			state.setPipeline(m_GradientPipeline);
			state.addBindingSet(bindings);
			commandList->setComputeState(state);
			glm::vec4 params[3] = { glm::vec4(settings.SkyColor, static_cast<float>(size)), glm::vec4(settings.GroundColor, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f) };
			commandList->setPushConstants(params, sizeof(params));
			commandList->dispatch(Groups(size), Groups(size), 6);
		}
	}

	void EnvironmentMap::FilterEnvironment(nvrhi::ICommandList* commandList)
	{
		nvrhi::IDevice* device = m_Context.GetDevice();
		auto cubeSRV = [&](nvrhi::ITexture* cube) {
			return nvrhi::BindingSetItem::Texture_SRV(0, cube, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube);
		};

		// Diffuse irradiance
		{
			nvrhi::BindingSetDesc set;
			set.addItem(cubeSRV(m_Environment));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Context.GetLinearClampSampler()));
			set.addItem(CubeUAV(2, m_Irradiance, 0));
			set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::vec4)));
			nvrhi::BindingSetHandle bindings = device->createBindingSet(set, m_FilterLayout);
			nvrhi::ComputeState state;
			state.setPipeline(m_IrradiancePipeline);
			state.addBindingSet(bindings);
			commandList->setComputeState(state);
			glm::vec4 params(static_cast<float>(IrradianceSize), 3.0f, 0.0f, 0.0f); // sample the environment from mip 3 (64x64) to smooth the integral
			commandList->setPushConstants(&params, sizeof(params));
			commandList->dispatch(Groups(IrradianceSize), Groups(IrradianceSize), 6);
		}

		// Specular prefilter: one dispatch per mip, roughness increasing with the mip.
		for(uint32_t mip = 0; mip < m_PrefilterMips; mip++)
		{
			uint32_t size = std::max(PrefilterSize >> mip, 1u);
			float roughness = static_cast<float>(mip) / static_cast<float>(m_PrefilterMips - 1);
			nvrhi::BindingSetDesc set;
			set.addItem(cubeSRV(m_Environment));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Context.GetLinearClampSampler()));
			set.addItem(CubeUAV(2, m_Prefiltered, mip));
			set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::vec4)));
			nvrhi::BindingSetHandle bindings = device->createBindingSet(set, m_FilterLayout);
			nvrhi::ComputeState state;
			state.setPipeline(m_PrefilterPipeline);
			state.addBindingSet(bindings);
			commandList->setComputeState(state);
			glm::vec4 params(static_cast<float>(size), roughness, static_cast<float>(EnvironmentSize), 0.0f);
			commandList->setPushConstants(&params, sizeof(params));
			commandList->dispatch(Groups(size), Groups(size), 6);
		}
	}

}
