#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Renderer/Material.h"
#include "Starfall/Renderer/Mesh.h"
#include "Starfall/Scene/Components.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Starfall {

	class Scene;
	class Entity;

	struct ModelNode
	{
		std::string Name;
		glm::vec3 Translation = { 0, 0, 0 };
		glm::vec3 Rotation = { 0, 0, 0 }; // Euler radians
		glm::vec3 Scale = { 1, 1, 1 };
		int Parent = -1;
		std::vector<int> Children;
		std::vector<uint32_t> Meshes; // indices into Model::Meshes (one per glTF primitive)
	};

	// Imported glTF asset: flat list of primitives (meshes), materials and the node hierarchy.
	class Model
	{
	public:
		std::string Path;
		std::vector<Ref<Mesh>> Meshes;
		std::vector<Ref<Material>> Materials;
		std::vector<ModelNode> Nodes;
		std::vector<int> RootNodes;
	};

	namespace ModelImporter {
		// Loads .gltf (with external or embedded resources) or .glb. Returns null on failure (reason logged).
		Ref<Model> LoadFromFile(const std::filesystem::path& file, const std::string& assetPath);
		Ref<Model> LoadFromMemory(const void* data, size_t size, const std::filesystem::path& resourceDirectory, const std::string& assetPath);
	}

	// Creates an entity hierarchy mirroring the model's nodes; meshes reference "<assetPath>#<index>".
	// Returns the root entity (named after the model). Returns an invalid Entity if the model cannot be loaded.
	Entity InstantiateModel(Scene& scene, const AssetPath& assetPath, Entity parent);

}
