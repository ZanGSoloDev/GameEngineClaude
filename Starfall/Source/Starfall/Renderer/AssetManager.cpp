#include "Starfall/Renderer/AssetManager.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Project/Project.h"

#include <algorithm>
#include <unordered_map>

namespace Starfall {

	namespace {

		struct Cache
		{
			std::unordered_map<std::string, Ref<Mesh>> Meshes;
			std::unordered_map<std::string, Ref<Model>> Models;
			std::unordered_map<std::string, Ref<Material>> Materials;
			std::unordered_map<std::string, Ref<Texture2D>> Textures; // key includes the sRGB flag
			std::unordered_map<std::string, Ref<HdrImageData>> Hdris;
			Ref<Material> DefaultMaterial;
			Ref<Texture2D> White;
			Ref<Texture2D> FlatNormal;
		};

		Cache& GetCache()
		{
			static Cache cache;
			return cache;
		}

		constexpr const char* BuiltinPrefix = "builtin://";

		Ref<Mesh> CreateBuiltin(const std::string& name)
		{
			if(name == "Cube") return MeshFactory::CreateCube();
			if(name == "Sphere") return MeshFactory::CreateSphere();
			if(name == "Plane") return MeshFactory::CreatePlane();
			if(name == "Quad") return MeshFactory::CreateQuad();
			if(name == "Cylinder") return MeshFactory::CreateCylinder();
			if(name == "Capsule") return MeshFactory::CreateCapsule();
			if(name == "Cone") return MeshFactory::CreateCone();
			return nullptr;
		}

		std::string TextureKey(const AssetPath& path, bool srgb) { return (srgb ? "srgb:" : "lin:") + path; }

	}

	bool AssetManager::IsBuiltinMesh(const AssetPath& path) { return path.rfind(BuiltinPrefix, 0) == 0; }

	std::vector<std::string> AssetManager::GetBuiltinMeshNames()
	{
		return { "Cube", "Sphere", "Plane", "Quad", "Cylinder", "Capsule", "Cone" };
	}

	Ref<Mesh> AssetManager::GetMesh(const AssetPath& path)
	{
		Cache& cache = GetCache();
		if(auto it = cache.Meshes.find(path); it != cache.Meshes.end())
			return it->second;

		Ref<Mesh> mesh;
		if(IsBuiltinMesh(path))
		{
			mesh = CreateBuiltin(path.substr(std::char_traits<char>::length(BuiltinPrefix)));
			if(mesh)
				mesh->SetDefaultMaterial(GetDefaultMaterial());
			else
				SF_CORE_WARN("Unknown builtin mesh '{0}'", path);
		}
		else
		{
			size_t hash = path.rfind('#');
			if(hash == std::string::npos)
			{
				SF_CORE_WARN("Mesh path '{0}' must be 'builtin://Name' or 'Model.gltf#index'", path);
			}
			else
			{
				AssetPath modelPath = path.substr(0, hash);
				std::string indexText = path.substr(hash + 1);
				size_t index = 0;
				bool numeric = !indexText.empty() && std::all_of(indexText.begin(), indexText.end(), [](char c) { return c >= '0' && c <= '9'; });
				if(numeric && indexText.size() < 9)
					index = static_cast<size_t>(std::stoul(indexText));
				Ref<Model> model = GetModel(modelPath);
				if(model && numeric && index < model->Meshes.size())
					mesh = model->Meshes[index];
				else if(model)
					SF_CORE_WARN("Mesh '{0}' not found in model", path);
			}
		}
		cache.Meshes[path] = mesh;
		return mesh;
	}

	Ref<Model> AssetManager::GetModel(const AssetPath& path)
	{
		Cache& cache = GetCache();
		if(auto it = cache.Models.find(path); it != cache.Models.end())
			return it->second;
		std::filesystem::path file = Project::ResolvePath(path);
		Ref<Model> model = file.empty() ? nullptr : ModelImporter::LoadFromFile(file, path);
		if(!model)
			SF_CORE_WARN("Could not load model '{0}'", path);
		cache.Models[path] = model;
		return model;
	}

	Ref<Material> AssetManager::GetDefaultMaterial()
	{
		Cache& cache = GetCache();
		if(!cache.DefaultMaterial)
		{
			cache.DefaultMaterial = CreateRef<Material>("Default");
			cache.DefaultMaterial->GetData().BaseColor = { 0.8f, 0.8f, 0.8f, 1.0f };
			cache.DefaultMaterial->GetData().Roughness = 0.6f;
		}
		return cache.DefaultMaterial;
	}

	Ref<Material> AssetManager::GetMaterial(const AssetPath& path)
	{
		if(path.empty())
			return GetDefaultMaterial();
		Cache& cache = GetCache();
		if(auto it = cache.Materials.find(path); it != cache.Materials.end())
			return it->second;
		std::filesystem::path file = Project::ResolvePath(path);
		auto text = file.empty() ? std::nullopt : FileSystem::ReadText(file);
		Ref<Material> material = text ? MaterialSerializer::FromString(*text, std::filesystem::path(path).stem().string()) : nullptr;
		if(!material)
			SF_CORE_WARN("Could not load material '{0}'", path);
		cache.Materials[path] = material;
		return material;
	}

	Ref<Texture2D> AssetManager::GetTexture(const AssetPath& path, bool srgb)
	{
		if(path.empty())
			return nullptr;
		Cache& cache = GetCache();
		std::string key = TextureKey(path, srgb);
		if(auto it = cache.Textures.find(key); it != cache.Textures.end())
			return it->second;
		std::filesystem::path file = Project::ResolvePath(path);
		Ref<Texture2D> texture = file.empty() ? nullptr : Texture2D::LoadFromFile(file, srgb);
		cache.Textures[key] = texture;
		return texture;
	}

	Ref<HdrImageData> AssetManager::GetHDRI(const AssetPath& path)
	{
		if(path.empty())
			return nullptr;
		Cache& cache = GetCache();
		if(auto it = cache.Hdris.find(path); it != cache.Hdris.end())
			return it->second;
		std::filesystem::path file = Project::ResolvePath(path);
		Ref<HdrImageData> image;
		if(!file.empty())
		{
			auto hdr = CreateRef<HdrImageData>();
			if(ImageIO::LoadHDR(file, *hdr))
				image = hdr;
		}
		if(!image)
			SF_CORE_WARN("Could not load HDRI '{0}'", path);
		cache.Hdris[path] = image;
		return image;
	}

	Ref<Texture2D> AssetManager::GetWhiteTexture()
	{
		Cache& cache = GetCache();
		if(!cache.White)
			cache.White = Texture2D::CreateFromPixels("White", 1, 1, { 255, 255, 255, 255 }, false, false);
		return cache.White;
	}

	Ref<Texture2D> AssetManager::GetFlatNormalTexture()
	{
		Cache& cache = GetCache();
		if(!cache.FlatNormal)
			cache.FlatNormal = Texture2D::CreateFromPixels("FlatNormal", 1, 1, { 128, 128, 255, 255 }, false, false);
		return cache.FlatNormal;
	}

	void AssetManager::Invalidate(const AssetPath& path)
	{
		Cache& cache = GetCache();
		// A changed model invalidates its meshes ("path#n"); a changed texture invalidates materials that referenced it.
		std::erase_if(cache.Meshes, [&](const auto& kv) { return kv.first == path || kv.first.rfind(path + "#", 0) == 0; });
		cache.Models.erase(path);
		cache.Materials.erase(path);
		cache.Textures.erase(TextureKey(path, true));
		cache.Textures.erase(TextureKey(path, false));
		cache.Hdris.erase(path);
		if(!path.ends_with(".sfmat"))
			cache.Materials.clear();
	}

	void AssetManager::Clear()
	{
		Cache& cache = GetCache();
		cache = Cache();
	}

	void AssetManager::ReleaseGPU()
	{
		Cache& cache = GetCache();
		auto releaseMaterial = [](const Ref<Material>& material) {
			if(!material)
				return;
			material->ConstantBuffer = nullptr;
			material->BindingSet = nullptr;
			material->MarkDirty();
			for(size_t slot = 0; slot < static_cast<size_t>(TextureSlot::Count); slot++)
				if(const Ref<Texture2D>& texture = material->GetTexture(static_cast<TextureSlot>(slot)))
					texture->ReleaseGPU();
		};
		auto releaseMesh = [&](const Ref<Mesh>& mesh) {
			if(!mesh)
				return;
			mesh->ReleaseGPU();
			releaseMaterial(mesh->GetDefaultMaterial());
		};

		for(auto& [key, mesh] : cache.Meshes)
			releaseMesh(mesh);
		for(auto& [key, tex] : cache.Textures)
			if(tex)
				tex->ReleaseGPU();
		for(auto& [key, mat] : cache.Materials)
			releaseMaterial(mat);
		// Models own meshes/materials/textures that are not in the other caches.
		for(auto& [key, model] : cache.Models)
		{
			if(!model)
				continue;
			for(const Ref<Mesh>& mesh : model->Meshes)
				releaseMesh(mesh);
			for(const Ref<Material>& material : model->Materials)
				releaseMaterial(material);
		}
		releaseMaterial(cache.DefaultMaterial);
		if(cache.White) cache.White->ReleaseGPU();
		if(cache.FlatNormal) cache.FlatNormal->ReleaseGPU();
	}

	size_t AssetManager::GetCachedMeshCount() { return GetCache().Meshes.size(); }
	size_t AssetManager::GetCachedTextureCount() { return GetCache().Textures.size(); }

}
