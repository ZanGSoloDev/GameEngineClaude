#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Renderer/Texture.h"
#include "Starfall/Scene/Components.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <string>

namespace Starfall {

	enum class AlphaMode { Opaque = 0, Mask = 1, Blend = 2 };

	// Metallic-roughness PBR parameters (glTF 2.0 model).
	struct MaterialData
	{
		glm::vec4 BaseColor = { 1, 1, 1, 1 };
		float Metallic = 0.0f;
		float Roughness = 0.6f;
		glm::vec3 Emissive = { 0, 0, 0 };
		float EmissiveIntensity = 1.0f;
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		float AlphaCutoff = 0.5f;
		AlphaMode Alpha = AlphaMode::Opaque;
		bool DoubleSided = false;
		glm::vec2 UVScale = { 1, 1 };
		glm::vec2 UVOffset = { 0, 0 };
	};

	enum class TextureSlot { BaseColor = 0, MetallicRoughness = 1, Normal = 2, Occlusion = 3, Emissive = 4, Count = 5 };

	class Material
	{
	public:
		explicit Material(std::string name = "Material") : m_Name(std::move(name)) {}

		MaterialData& GetData() { return m_Data; }
		const MaterialData& GetData() const { return m_Data; }
		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		// Texture references: either loaded directly (glTF embedded) or by project path (.sfmat files).
		void SetTexture(TextureSlot slot, Ref<Texture2D> texture) { m_Textures[static_cast<size_t>(slot)] = std::move(texture); m_Dirty = true; }
		const Ref<Texture2D>& GetTexture(TextureSlot slot) const { return m_Textures[static_cast<size_t>(slot)]; }
		void SetTexturePath(TextureSlot slot, const AssetPath& path) { m_TexturePaths[static_cast<size_t>(slot)] = path; }
		const AssetPath& GetTexturePath(TextureSlot slot) const { return m_TexturePaths[static_cast<size_t>(slot)]; }

		// Marks GPU state (constant buffer / binding set) stale after parameters or textures changed.
		void MarkDirty() { m_Dirty = true; }
		bool IsDirty() const { return m_Dirty; }
		void ClearDirty() { m_Dirty = false; }

		// Renderer-owned GPU state.
		nvrhi::BufferHandle ConstantBuffer;
		nvrhi::BindingSetHandle BindingSet;

	private:
		std::string m_Name;
		MaterialData m_Data;
		Ref<Texture2D> m_Textures[static_cast<size_t>(TextureSlot::Count)];
		AssetPath m_TexturePaths[static_cast<size_t>(TextureSlot::Count)];
		bool m_Dirty = true;
	};

	// .sfmat (JSON) persistence. Texture paths are project-relative asset paths.
	namespace MaterialSerializer {
		std::string ToString(const Material& material);
		// Loads parameters and resolves texture paths through the AssetManager.
		Ref<Material> FromString(const std::string& text, const std::string& name);
		bool Save(const Material& material, const std::filesystem::path& file);
	}

}
