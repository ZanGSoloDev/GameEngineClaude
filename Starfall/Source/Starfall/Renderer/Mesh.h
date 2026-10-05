#pragma once

#include "Starfall/Core/Base.h"
#include "Starfall/Math/MathUtils.h"

#include <nvrhi/nvrhi.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Starfall {

	class Material;

	struct Vertex
	{
		glm::vec3 Position = { 0, 0, 0 };
		glm::vec3 Normal = { 0, 1, 0 };
		glm::vec4 Tangent = { 1, 0, 0, 1 }; // xyz tangent, w handedness
		glm::vec2 UV = { 0, 0 };
	};
	static_assert(sizeof(Vertex) == 48, "Vertex layout is shared with the shaders");

	// Triangle mesh. CPU data is kept for picking/physics; GPU buffers are created on first use by the renderer.
	class Mesh
	{
	public:
		Mesh(std::string name, std::vector<Vertex> vertices, std::vector<uint32_t> indices);

		const std::string& GetName() const { return m_Name; }
		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }
		const Math::AABB& GetBounds() const { return m_Bounds; }

		// Material imported together with the mesh (glTF); used when a MeshRenderer has no material override.
		const Ref<Material>& GetDefaultMaterial() const { return m_DefaultMaterial; }
		void SetDefaultMaterial(Ref<Material> material) { m_DefaultMaterial = std::move(material); }

		// Closest triangle hit in mesh space (ray must be in mesh space). Returns true and the distance on hit.
		bool Raycast(const Math::Ray& localRay, float& outDistance) const;

		// Generates tangents from UVs when the source mesh has none.
		void RecalculateTangents();

		// GPU resources: created on demand through the given open command list.
		void EnsureGPU(nvrhi::IDevice* device, nvrhi::ICommandList* commandList);
		bool IsUploaded() const { return m_VertexBuffer != nullptr; }
		nvrhi::IBuffer* GetVertexBuffer() const { return m_VertexBuffer; }
		nvrhi::IBuffer* GetIndexBuffer() const { return m_IndexBuffer; }
		void ReleaseGPU();

	private:
		std::string m_Name;
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		Math::AABB m_Bounds;
		Ref<Material> m_DefaultMaterial;
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
	};

	// Procedural primitives, all unit sized and centered at the origin (Y up).
	namespace MeshFactory {
		Ref<Mesh> CreateCube();
		Ref<Mesh> CreateSphere(uint32_t segments = 48, uint32_t rings = 24);
		Ref<Mesh> CreatePlane();   // 1x1 in XZ, facing +Y
		Ref<Mesh> CreateQuad();    // 1x1 in XY, facing +Z
		Ref<Mesh> CreateCylinder(uint32_t segments = 48);
		Ref<Mesh> CreateCapsule(uint32_t segments = 32, uint32_t rings = 12); // radius 0.5, total height 2
		Ref<Mesh> CreateCone(uint32_t segments = 48);
	}

}
