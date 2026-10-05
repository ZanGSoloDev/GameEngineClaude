#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "Starfall/Renderer/Model.h"

#include "Starfall/Core/FileSystem.h"
#include "Starfall/Math/MathUtils.h"
#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Scene/Entity.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <map>

namespace Starfall {

	namespace ModelImporter {

		namespace {

			struct CgltfDeleter
			{
				void operator()(cgltf_data* data) const { cgltf_free(data); }
			};
			using CgltfPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

			std::string DecodeUriPath(const char* uri)
			{
				std::string decoded = uri;
				cgltf_decode_uri(decoded.data());
				decoded.resize(std::strlen(decoded.c_str()));
				return decoded;
			}

			// Decodes image `index` (embedded buffer view, data URI or external file) into a texture.
			Ref<Texture2D> LoadImage(const cgltf_data* data, const cgltf_image* image, const std::filesystem::path& directory, const std::string& name, bool srgb)
			{
				if(image->buffer_view)
				{
					const cgltf_buffer_view* view = image->buffer_view;
					if(!view->buffer->data)
						return nullptr;
					return Texture2D::LoadFromMemory(name, static_cast<const uint8_t*>(view->buffer->data) + view->offset, view->size, srgb);
				}
				if(!image->uri)
					return nullptr;
				std::string uri = image->uri;
				if(uri.rfind("data:", 0) == 0)
				{
					size_t comma = uri.find(',');
					if(comma == std::string::npos)
						return nullptr;
					size_t base64Size = uri.size() - comma - 1;
					size_t decodedSize = base64Size / 4 * 3;
					void* decoded = nullptr;
					cgltf_options options{};
					if(cgltf_load_buffer_base64(&options, decodedSize, uri.c_str() + comma + 1, &decoded) != cgltf_result_success)
						return nullptr;
					Ref<Texture2D> texture = Texture2D::LoadFromMemory(name, decoded, decodedSize, srgb);
					free(decoded);
					return texture;
				}
				std::string decodedUri = DecodeUriPath(uri.c_str());
				std::filesystem::path file = directory / std::filesystem::path(std::u8string(decodedUri.begin(), decodedUri.end()));
				// Reject paths that escape the model directory.
				std::filesystem::path normalized = file.lexically_normal();
				std::filesystem::path root = directory.lexically_normal();
				auto rel = normalized.lexically_relative(root);
				if(rel.empty() || *rel.begin() == "..")
				{
					SF_CORE_WARN("glTF image '{0}' points outside the model directory; ignored", uri);
					return nullptr;
				}
				(void)data;
				return Texture2D::LoadFromFile(normalized, srgb);
			}

			class TextureCache
			{
			public:
				TextureCache(const cgltf_data* data, std::filesystem::path directory, std::string modelName)
					: m_Data(data), m_Directory(std::move(directory)), m_ModelName(std::move(modelName)) {}

				Ref<Texture2D> Get(const cgltf_texture_view& view, bool srgb)
				{
					if(!view.texture || !view.texture->image)
						return nullptr;
					size_t index = cgltf_image_index(m_Data, view.texture->image);
					auto key = std::make_pair(index, srgb);
					if(auto it = m_Cache.find(key); it != m_Cache.end())
						return it->second;
					std::string name = view.texture->image->name ? view.texture->image->name : (m_ModelName + "_image" + std::to_string(index));
					Ref<Texture2D> texture = LoadImage(m_Data, view.texture->image, m_Directory, name, srgb);
					m_Cache[key] = texture;
					return texture;
				}

			private:
				const cgltf_data* m_Data;
				std::filesystem::path m_Directory;
				std::string m_ModelName;
				std::map<std::pair<size_t, bool>, Ref<Texture2D>> m_Cache;
			};

			Ref<Material> ConvertMaterial(const cgltf_material* src, TextureCache& textures, size_t index)
			{
				std::string name = src->name ? src->name : "Material" + std::to_string(index);
				auto material = CreateRef<Material>(name);
				MaterialData& d = material->GetData();

				if(src->has_pbr_metallic_roughness)
				{
					const auto& pbr = src->pbr_metallic_roughness;
					d.BaseColor = { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
					d.Metallic = pbr.metallic_factor;
					d.Roughness = pbr.roughness_factor;
					material->SetTexture(TextureSlot::BaseColor, textures.Get(pbr.base_color_texture, true));
					material->SetTexture(TextureSlot::MetallicRoughness, textures.Get(pbr.metallic_roughness_texture, false));
				}
				else
				{
					d.Metallic = 0.0f; // glTF spec default when pbrMetallicRoughness is absent is metallic=1, but such files are specular/gloss; keep neutral
					d.Roughness = 1.0f;
				}
				material->SetTexture(TextureSlot::Normal, textures.Get(src->normal_texture, false));
				d.NormalScale = src->normal_texture.scale;
				material->SetTexture(TextureSlot::Occlusion, textures.Get(src->occlusion_texture, false));
				d.OcclusionStrength = src->occlusion_texture.scale;
				material->SetTexture(TextureSlot::Emissive, textures.Get(src->emissive_texture, true));
				d.Emissive = { src->emissive_factor[0], src->emissive_factor[1], src->emissive_factor[2] };
				if(src->has_emissive_strength)
					d.EmissiveIntensity = src->emissive_strength.emissive_strength;
				d.DoubleSided = src->double_sided != 0;
				switch(src->alpha_mode)
				{
					case cgltf_alpha_mode_mask: d.Alpha = AlphaMode::Mask; d.AlphaCutoff = src->alpha_cutoff; break;
					case cgltf_alpha_mode_blend: d.Alpha = AlphaMode::Blend; break;
					default: d.Alpha = AlphaMode::Opaque; break;
				}
				return material;
			}

			void ComputeNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
			{
				for(Vertex& v : vertices)
					v.Normal = glm::vec3(0.0f);
				for(size_t i = 0; i + 2 < indices.size(); i += 3)
				{
					Vertex& a = vertices[indices[i]];
					Vertex& b = vertices[indices[i + 1]];
					Vertex& c = vertices[indices[i + 2]];
					glm::vec3 n = glm::cross(b.Position - a.Position, c.Position - a.Position);
					a.Normal += n;
					b.Normal += n;
					c.Normal += n;
				}
				for(Vertex& v : vertices)
					v.Normal = glm::dot(v.Normal, v.Normal) > 1e-20f ? glm::normalize(v.Normal) : glm::vec3(0, 1, 0);
			}

			Ref<Mesh> ConvertPrimitive(const cgltf_primitive& prim, const std::string& name)
			{
				if(prim.type != cgltf_primitive_type_triangles)
					return nullptr;

				const cgltf_accessor* position = nullptr;
				const cgltf_accessor* normal = nullptr;
				const cgltf_accessor* tangent = nullptr;
				const cgltf_accessor* uv = nullptr;
				for(cgltf_size i = 0; i < prim.attributes_count; i++)
				{
					const cgltf_attribute& attr = prim.attributes[i];
					if(attr.index != 0)
						continue;
					switch(attr.type)
					{
						case cgltf_attribute_type_position: position = attr.data; break;
						case cgltf_attribute_type_normal: normal = attr.data; break;
						case cgltf_attribute_type_tangent: tangent = attr.data; break;
						case cgltf_attribute_type_texcoord: uv = attr.data; break;
						default: break;
					}
				}
				if(!position || position->count == 0 || position->count > (1u << 26))
					return nullptr;

				std::vector<Vertex> vertices(position->count);
				for(cgltf_size i = 0; i < position->count; i++)
				{
					float p[3] = {};
					cgltf_accessor_read_float(position, i, p, 3);
					vertices[i].Position = { p[0], p[1], p[2] };
					if(normal && i < normal->count)
					{
						float n[3] = {};
						cgltf_accessor_read_float(normal, i, n, 3);
						vertices[i].Normal = { n[0], n[1], n[2] };
					}
					if(uv && i < uv->count)
					{
						float t[2] = {};
						cgltf_accessor_read_float(uv, i, t, 2);
						vertices[i].UV = { t[0], t[1] };
					}
					if(tangent && i < tangent->count)
					{
						float t[4] = { 1, 0, 0, 1 };
						cgltf_accessor_read_float(tangent, i, t, 4);
						vertices[i].Tangent = { t[0], t[1], t[2], t[3] };
					}
				}

				std::vector<uint32_t> indices;
				if(prim.indices)
				{
					indices.resize(prim.indices->count);
					for(cgltf_size i = 0; i < prim.indices->count; i++)
					{
						indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
						if(indices[i] >= vertices.size())
							return nullptr; // corrupt file
					}
				}
				else
				{
					indices.resize(vertices.size());
					for(uint32_t i = 0; i < indices.size(); i++)
						indices[i] = i;
				}
				indices.resize(indices.size() / 3 * 3);
				if(indices.empty())
					return nullptr;

				if(!normal)
					ComputeNormals(vertices, indices);
				auto mesh = CreateRef<Mesh>(name, std::move(vertices), std::move(indices));
				if(!tangent)
					mesh->RecalculateTangents();
				return mesh;
			}

		}

		Ref<Model> LoadFromMemory(const void* bytes, size_t size, const std::filesystem::path& resourceDirectory, const std::string& assetPath)
		{
			cgltf_options options{};
			cgltf_data* raw = nullptr;
			if(cgltf_parse(&options, bytes, size, &raw) != cgltf_result_success)
			{
				SF_CORE_ERROR("glTF parse failed for '{0}'", assetPath);
				return nullptr;
			}
			CgltfPtr data(raw);

			// External buffers resolve relative to a (virtual) file inside the resource directory.
			std::string virtualFile = (resourceDirectory / "model.gltf").string();
			if(cgltf_load_buffers(&options, data.get(), virtualFile.c_str()) != cgltf_result_success)
			{
				SF_CORE_ERROR("glTF buffers could not be loaded for '{0}'", assetPath);
				return nullptr;
			}
			if(cgltf_validate(data.get()) != cgltf_result_success)
			{
				SF_CORE_ERROR("glTF validation failed for '{0}'", assetPath);
				return nullptr;
			}

			auto model = CreateRef<Model>();
			model->Path = assetPath;
			std::string modelName = std::filesystem::path(assetPath).stem().string();

			TextureCache textures(data.get(), resourceDirectory, modelName);
			for(cgltf_size i = 0; i < data->materials_count; i++)
				model->Materials.push_back(ConvertMaterial(&data->materials[i], textures, i));
			Ref<Material> fallback = AssetManager::GetDefaultMaterial();

			// glTF mesh -> list of our meshes (one per triangle primitive)
			std::vector<std::vector<uint32_t>> meshPrimitives(data->meshes_count);
			for(cgltf_size m = 0; m < data->meshes_count; m++)
			{
				const cgltf_mesh& mesh = data->meshes[m];
				for(cgltf_size p = 0; p < mesh.primitives_count; p++)
				{
					const cgltf_primitive& prim = mesh.primitives[p];
					std::string name = assetPath + "#" + std::to_string(model->Meshes.size());
					Ref<Mesh> converted = ConvertPrimitive(prim, name);
					if(!converted)
					{
						SF_CORE_WARN("Skipping unsupported/invalid primitive {0} of mesh {1} in '{2}'", p, m, assetPath);
						continue;
					}
					if(prim.material)
						converted->SetDefaultMaterial(model->Materials[cgltf_material_index(data.get(), prim.material)]);
					else
						converted->SetDefaultMaterial(fallback);
					meshPrimitives[m].push_back(static_cast<uint32_t>(model->Meshes.size()));
					model->Meshes.push_back(std::move(converted));
				}
			}

			// Nodes (flattened in file order)
			model->Nodes.resize(data->nodes_count);
			for(cgltf_size n = 0; n < data->nodes_count; n++)
			{
				const cgltf_node& src = data->nodes[n];
				ModelNode& node = model->Nodes[n];
				node.Name = src.name ? src.name : "Node" + std::to_string(n);
				float matrix[16];
				cgltf_node_transform_local(&src, matrix);
				glm::mat4 local = glm::make_mat4(matrix);
				Math::DecomposeTransform(local, node.Translation, node.Rotation, node.Scale);
				if(src.parent)
					node.Parent = static_cast<int>(cgltf_node_index(data.get(), src.parent));
				for(cgltf_size c = 0; c < src.children_count; c++)
					node.Children.push_back(static_cast<int>(cgltf_node_index(data.get(), src.children[c])));
				if(src.mesh)
					node.Meshes = meshPrimitives[cgltf_mesh_index(data.get(), src.mesh)];
			}

			const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
			if(scene)
			{
				for(cgltf_size i = 0; i < scene->nodes_count; i++)
					model->RootNodes.push_back(static_cast<int>(cgltf_node_index(data.get(), scene->nodes[i])));
			}
			else
			{
				for(size_t n = 0; n < model->Nodes.size(); n++)
					if(model->Nodes[n].Parent < 0)
						model->RootNodes.push_back(static_cast<int>(n));
			}

			if(model->Meshes.empty())
			{
				SF_CORE_ERROR("glTF '{0}' contains no triangle meshes", assetPath);
				return nullptr;
			}
			return model;
		}

		Ref<Model> LoadFromFile(const std::filesystem::path& file, const std::string& assetPath)
		{
			auto bytes = FileSystem::ReadBinary(file);
			if(!bytes || bytes->empty())
			{
				SF_CORE_ERROR("Could not read model '{0}'", file.string());
				return nullptr;
			}
			return LoadFromMemory(bytes->data(), bytes->size(), file.parent_path(), assetPath);
		}

	}

	Entity InstantiateModel(Scene& scene, const AssetPath& assetPath, Entity parent)
	{
		Ref<Model> model = AssetManager::GetModel(assetPath);
		if(!model)
			return {};

		Entity root = scene.CreateEntity(std::filesystem::path(assetPath).stem().string());
		if(parent)
			scene.SetParent(root, parent, false);

		std::vector<Entity> entities(model->Nodes.size());
		// Create in an order that guarantees parents exist: depth-first from the roots.
		std::vector<std::pair<int, Entity>> stack;
		for(auto it = model->RootNodes.rbegin(); it != model->RootNodes.rend(); ++it)
			stack.emplace_back(*it, root);
		while(!stack.empty())
		{
			auto [index, parentEntity] = stack.back();
			stack.pop_back();
			const ModelNode& node = model->Nodes[static_cast<size_t>(index)];
			Entity entity = scene.CreateEntity(node.Name);
			scene.SetParent(entity, parentEntity, false);
			auto& tc = entity.Transform();
			tc.Translation = node.Translation;
			tc.Rotation = node.Rotation;
			tc.Scale = node.Scale;
			entities[static_cast<size_t>(index)] = entity;

			if(node.Meshes.size() == 1)
			{
				entity.AddComponent<MeshRendererComponent>().Mesh = assetPath + "#" + std::to_string(node.Meshes[0]);
			}
			else
			{
				for(uint32_t meshIndex : node.Meshes)
				{
					Entity part = scene.CreateEntity(model->Meshes[meshIndex]->GetName());
					scene.SetParent(part, entity, false);
					part.AddComponent<MeshRendererComponent>().Mesh = assetPath + "#" + std::to_string(meshIndex);
				}
			}
			for(auto child = node.Children.rbegin(); child != node.Children.rend(); ++child)
				stack.emplace_back(*child, entity);
		}
		return root;
	}

}
