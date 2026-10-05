#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Renderer/Material.h"
#include "Starfall/Renderer/Mesh.h"
#include "Starfall/Renderer/Model.h"
#include "Starfall/Renderer/Texture.h"
#include "Starfall/Scene/Components.h"

namespace Starfall {

	// Central cache for CPU-side assets, keyed by project-relative path. Failed loads are cached too (one log line, no
	// repeated disk access) until Invalidate/Clear. Not thread safe: use from the main thread.
	class AssetManager
	{
	public:
		// "builtin://Cube|Sphere|Plane|Quad|Cylinder|Capsule|Cone", or "Models/File.gltf#<primitive index>".
		static Ref<Mesh> GetMesh(const AssetPath& path);
		static Ref<Model> GetModel(const AssetPath& path);
		// "" returns the default material; otherwise a ".sfmat" file.
		static Ref<Material> GetMaterial(const AssetPath& path);
		static Ref<Material> GetDefaultMaterial();
		static Ref<Texture2D> GetTexture(const AssetPath& path, bool srgb);
		static Ref<HdrImageData> GetHDRI(const AssetPath& path);

		// 1x1 fallbacks bound when a material has no texture in a slot.
		static Ref<Texture2D> GetWhiteTexture();
		static Ref<Texture2D> GetFlatNormalTexture();

		static bool IsBuiltinMesh(const AssetPath& path);
		static std::vector<std::string> GetBuiltinMeshNames();

		// Drops cached data for one asset (and anything derived from it) so it is reloaded on next use.
		static void Invalidate(const AssetPath& path);
		static void Clear();
		// Releases GPU resources of all cached assets (device shutdown / device loss).
		static void ReleaseGPU();

		static size_t GetCachedMeshCount();
		static size_t GetCachedTextureCount();
	};

}
