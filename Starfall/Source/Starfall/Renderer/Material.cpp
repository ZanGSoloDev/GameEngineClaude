#include "Starfall/Renderer/Material.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Renderer/AssetManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Starfall::MaterialSerializer {

	using json = nlohmann::json;

	namespace {

		constexpr const char* SlotNames[] = { "BaseColor", "MetallicRoughness", "Normal", "Occlusion", "Emissive" };

		bool IsSRGBSlot(TextureSlot slot) { return slot == TextureSlot::BaseColor || slot == TextureSlot::Emissive; }

		glm::vec3 ReadVec3(const json& j, const glm::vec3& fallback)
		{
			if(!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number())
				return fallback;
			return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
		}

		glm::vec2 ReadVec2(const json& j, const glm::vec2& fallback)
		{
			if(!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number())
				return fallback;
			return { j[0].get<float>(), j[1].get<float>() };
		}

	}

	std::string ToString(const Material& material)
	{
		const MaterialData& d = material.GetData();
		json j;
		j["Version"] = 1;
		j["Type"] = "Material";
		j["BaseColor"] = { d.BaseColor.r, d.BaseColor.g, d.BaseColor.b, d.BaseColor.a };
		j["Metallic"] = d.Metallic;
		j["Roughness"] = d.Roughness;
		j["Emissive"] = { d.Emissive.r, d.Emissive.g, d.Emissive.b };
		j["EmissiveIntensity"] = d.EmissiveIntensity;
		j["NormalScale"] = d.NormalScale;
		j["OcclusionStrength"] = d.OcclusionStrength;
		j["AlphaCutoff"] = d.AlphaCutoff;
		j["AlphaMode"] = static_cast<int>(d.Alpha);
		j["DoubleSided"] = d.DoubleSided;
		j["UVScale"] = { d.UVScale.x, d.UVScale.y };
		j["UVOffset"] = { d.UVOffset.x, d.UVOffset.y };
		json textures = json::object();
		for(size_t i = 0; i < static_cast<size_t>(TextureSlot::Count); i++)
			if(!material.GetTexturePath(static_cast<TextureSlot>(i)).empty())
				textures[SlotNames[i]] = material.GetTexturePath(static_cast<TextureSlot>(i));
		j["Textures"] = textures;
		return j.dump(2);
	}

	Ref<Material> FromString(const std::string& text, const std::string& name)
	{
		json j = json::parse(text, nullptr, false);
		if(j.is_discarded() || !j.is_object() || j.value("Type", "") != "Material")
			return nullptr;

		auto material = CreateRef<Material>(name);
		MaterialData& d = material->GetData();
		if(j.contains("BaseColor") && j["BaseColor"].is_array() && j["BaseColor"].size() == 4)
		{
			const json& c = j["BaseColor"];
			bool numeric = std::all_of(c.begin(), c.end(), [](const json& v) { return v.is_number(); });
			if(numeric)
				d.BaseColor = { c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>() };
		}
		d.Metallic = std::clamp(j.value("Metallic", d.Metallic), 0.0f, 1.0f);
		d.Roughness = std::clamp(j.value("Roughness", d.Roughness), 0.0f, 1.0f);
		d.Emissive = ReadVec3(j.value("Emissive", json()), d.Emissive);
		d.EmissiveIntensity = std::max(j.value("EmissiveIntensity", d.EmissiveIntensity), 0.0f);
		d.NormalScale = j.value("NormalScale", d.NormalScale);
		d.OcclusionStrength = std::clamp(j.value("OcclusionStrength", d.OcclusionStrength), 0.0f, 1.0f);
		d.AlphaCutoff = std::clamp(j.value("AlphaCutoff", d.AlphaCutoff), 0.0f, 1.0f);
		int alpha = j.value("AlphaMode", 0);
		d.Alpha = alpha >= 0 && alpha <= 2 ? static_cast<AlphaMode>(alpha) : AlphaMode::Opaque;
		d.DoubleSided = j.value("DoubleSided", false);
		d.UVScale = ReadVec2(j.value("UVScale", json()), d.UVScale);
		d.UVOffset = ReadVec2(j.value("UVOffset", json()), d.UVOffset);

		if(j.contains("Textures") && j["Textures"].is_object())
		{
			for(size_t i = 0; i < static_cast<size_t>(TextureSlot::Count); i++)
			{
				auto it = j["Textures"].find(SlotNames[i]);
				if(it == j["Textures"].end() || !it->is_string())
					continue;
				TextureSlot slot = static_cast<TextureSlot>(i);
				material->SetTexturePath(slot, it->get<std::string>());
				material->SetTexture(slot, AssetManager::GetTexture(it->get<std::string>(), IsSRGBSlot(slot)));
			}
		}
		return material;
	}

	bool Save(const Material& material, const std::filesystem::path& file)
	{
		return FileSystem::WriteText(file, ToString(material));
	}

}
