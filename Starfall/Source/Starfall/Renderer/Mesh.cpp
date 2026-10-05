#include "Starfall/Renderer/Mesh.h"

#include "Starfall/Renderer/Material.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Starfall {

	Mesh::Mesh(std::string name, std::vector<Vertex> vertices, std::vector<uint32_t> indices)
		: m_Name(std::move(name)), m_Vertices(std::move(vertices)), m_Indices(std::move(indices))
	{
		for(const Vertex& v : m_Vertices)
			m_Bounds.Expand(v.Position);
	}

	bool Mesh::Raycast(const Math::Ray& ray, float& outDistance) const
	{
		float boxT;
		if(!Math::IntersectRayAABB(ray, m_Bounds, boxT))
			return false;

		bool hit = false;
		float best = 1e30f;
		for(size_t i = 0; i + 2 < m_Indices.size(); i += 3)
		{
			float t;
			if(Math::IntersectRayTriangle(ray, m_Vertices[m_Indices[i]].Position, m_Vertices[m_Indices[i + 1]].Position, m_Vertices[m_Indices[i + 2]].Position, t) && t < best)
			{
				best = t;
				hit = true;
			}
		}
		if(hit)
			outDistance = best;
		return hit;
	}

	void Mesh::RecalculateTangents()
	{
		std::vector<glm::vec3> tan(m_Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitan(m_Vertices.size(), glm::vec3(0.0f));
		for(size_t i = 0; i + 2 < m_Indices.size(); i += 3)
		{
			uint32_t i0 = m_Indices[i], i1 = m_Indices[i + 1], i2 = m_Indices[i + 2];
			const Vertex& v0 = m_Vertices[i0];
			const Vertex& v1 = m_Vertices[i1];
			const Vertex& v2 = m_Vertices[i2];
			glm::vec3 e1 = v1.Position - v0.Position;
			glm::vec3 e2 = v2.Position - v0.Position;
			glm::vec2 d1 = v1.UV - v0.UV;
			glm::vec2 d2 = v2.UV - v0.UV;
			float det = d1.x * d2.y - d2.x * d1.y;
			if(std::abs(det) < 1e-12f)
				continue;
			float r = 1.0f / det;
			glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
			glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
			for(uint32_t idx : { i0, i1, i2 })
			{
				tan[idx] += t;
				bitan[idx] += b;
			}
		}
		for(size_t i = 0; i < m_Vertices.size(); i++)
		{
			glm::vec3 n = m_Vertices[i].Normal;
			glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
			if(glm::dot(t, t) < 1e-12f)
			{
				// Degenerate UVs: pick any vector perpendicular to the normal.
				t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0));
			}
			t = glm::normalize(t);
			float handedness = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
			m_Vertices[i].Tangent = glm::vec4(t, handedness);
		}
	}

	void Mesh::EnsureGPU(nvrhi::IDevice* device, nvrhi::ICommandList* commandList)
	{
		if(m_VertexBuffer || m_Vertices.empty() || m_Indices.empty())
			return;

		nvrhi::BufferDesc vb;
		vb.byteSize = m_Vertices.size() * sizeof(Vertex);
		vb.isVertexBuffer = true;
		vb.debugName = m_Name + " VB";
		vb.initialState = nvrhi::ResourceStates::VertexBuffer;
		vb.keepInitialState = true;
		m_VertexBuffer = device->createBuffer(vb);
		commandList->writeBuffer(m_VertexBuffer, m_Vertices.data(), vb.byteSize);

		nvrhi::BufferDesc ib;
		ib.byteSize = m_Indices.size() * sizeof(uint32_t);
		ib.isIndexBuffer = true;
		ib.debugName = m_Name + " IB";
		ib.initialState = nvrhi::ResourceStates::IndexBuffer;
		ib.keepInitialState = true;
		m_IndexBuffer = device->createBuffer(ib);
		commandList->writeBuffer(m_IndexBuffer, m_Indices.data(), ib.byteSize);
	}

	void Mesh::ReleaseGPU()
	{
		m_VertexBuffer = nullptr;
		m_IndexBuffer = nullptr;
	}

	namespace MeshFactory {

		namespace {

			void AddQuad(std::vector<Vertex>& v, std::vector<uint32_t>& idx, glm::vec3 origin, glm::vec3 u, glm::vec3 w, glm::vec3 normal)
			{
				// Quad spanned by u (uv.x) and w (uv.y) from origin; winding is counter-clockwise seen along -normal.
				uint32_t base = static_cast<uint32_t>(v.size());
				glm::vec4 tangent(glm::normalize(u), 1.0f);
				v.push_back({ origin, normal, tangent, { 0, 1 } });
				v.push_back({ origin + u, normal, tangent, { 1, 1 } });
				v.push_back({ origin + u + w, normal, tangent, { 1, 0 } });
				v.push_back({ origin + w, normal, tangent, { 0, 0 } });
				for(uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
					idx.push_back(base + i);
			}

			// Builds a revolved surface from a profile of (radius, y, normalRadial, normalY) rings.
			struct ProfilePoint { float Radius; float Y; float NormalRadial; float NormalY; float V; };

			Ref<Mesh> Revolve(const char* name, const std::vector<ProfilePoint>& profile, uint32_t segments)
			{
				std::vector<Vertex> vertices;
				std::vector<uint32_t> indices;
				for(const ProfilePoint& p : profile)
				{
					for(uint32_t s = 0; s <= segments; s++)
					{
						float u = static_cast<float>(s) / segments;
						float angle = u * glm::two_pi<float>();
						float cs = std::cos(angle), sn = std::sin(angle);
						Vertex vert;
						vert.Position = { p.Radius * cs, p.Y, p.Radius * sn };
						glm::vec3 n(p.NormalRadial * cs, p.NormalY, p.NormalRadial * sn);
						vert.Normal = glm::length(n) > 1e-6f ? glm::normalize(n) : glm::vec3(0, 1, 0);
						vert.Tangent = glm::vec4(-sn, 0.0f, cs, 1.0f);
						vert.UV = { u, p.V };
						vertices.push_back(vert);
					}
				}
				uint32_t stride = segments + 1;
				for(uint32_t r = 0; r + 1 < profile.size(); r++)
				{
					for(uint32_t s = 0; s < segments; s++)
					{
						uint32_t a = r * stride + s, b = a + 1, c = a + stride, d = c + 1;
						// Outward-facing CCW winding for profiles that go from top (+Y) to bottom (-Y).
						indices.insert(indices.end(), { a, b, c, b, d, c });
					}
				}
				auto mesh = CreateRef<Mesh>(name, std::move(vertices), std::move(indices));
				return mesh;
			}

		}

		Ref<Mesh> CreateCube()
		{
			std::vector<Vertex> v;
			std::vector<uint32_t> i;
			const float h = 0.5f;
			AddQuad(v, i, { -h, -h, h }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });   // +Z
			AddQuad(v, i, { h, -h, -h }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 }); // -Z
			AddQuad(v, i, { h, -h, h }, { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 });   // +X
			AddQuad(v, i, { -h, -h, -h }, { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 }); // -X
			AddQuad(v, i, { -h, h, h }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 });   // +Y
			AddQuad(v, i, { -h, -h, -h }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 }); // -Y
			return CreateRef<Mesh>("builtin://Cube", std::move(v), std::move(i));
		}

		Ref<Mesh> CreatePlane()
		{
			std::vector<Vertex> v;
			std::vector<uint32_t> i;
			AddQuad(v, i, { -0.5f, 0, 0.5f }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 });
			return CreateRef<Mesh>("builtin://Plane", std::move(v), std::move(i));
		}

		Ref<Mesh> CreateQuad()
		{
			std::vector<Vertex> v;
			std::vector<uint32_t> i;
			AddQuad(v, i, { -0.5f, -0.5f, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
			return CreateRef<Mesh>("builtin://Quad", std::move(v), std::move(i));
		}

		Ref<Mesh> CreateSphere(uint32_t segments, uint32_t rings)
		{
			std::vector<ProfilePoint> profile;
			for(uint32_t r = 0; r <= rings; r++)
			{
				float t = static_cast<float>(r) / rings;
				float phi = t * glm::pi<float>(); // 0 = top
				float radius = std::sin(phi) * 0.5f;
				float y = std::cos(phi) * 0.5f;
				profile.push_back({ radius, y, std::sin(phi), std::cos(phi), t });
			}
			auto mesh = Revolve("builtin://Sphere", profile, segments);
			return mesh;
		}

		Ref<Mesh> CreateCylinder(uint32_t segments)
		{
			std::vector<Vertex> verts;
			std::vector<uint32_t> idx;
			auto side = Revolve("side", { { 0.5f, 0.5f, 1, 0, 0 }, { 0.5f, -0.5f, 1, 0, 1 } }, segments);
			verts = side->GetVertices();
			idx = side->GetIndices();
			for(int cap = 0; cap < 2; cap++)
			{
				float y = cap == 0 ? 0.5f : -0.5f;
				float ny = cap == 0 ? 1.0f : -1.0f;
				uint32_t center = static_cast<uint32_t>(verts.size());
				verts.push_back({ { 0, y, 0 }, { 0, ny, 0 }, { 1, 0, 0, 1 }, { 0.5f, 0.5f } });
				for(uint32_t s = 0; s <= segments; s++)
				{
					float a = static_cast<float>(s) / segments * glm::two_pi<float>();
					float cs = std::cos(a), sn = std::sin(a);
					verts.push_back({ { 0.5f * cs, y, 0.5f * sn }, { 0, ny, 0 }, { 1, 0, 0, 1 }, { 0.5f + 0.5f * cs, 0.5f + 0.5f * sn } });
				}
				for(uint32_t s = 0; s < segments; s++)
				{
					uint32_t a = center + 1 + s, b = a + 1;
					if(cap == 0)
						idx.insert(idx.end(), { center, b, a });
					else
						idx.insert(idx.end(), { center, a, b });
				}
			}
			return CreateRef<Mesh>("builtin://Cylinder", std::move(verts), std::move(idx));
		}

		Ref<Mesh> CreateCapsule(uint32_t segments, uint32_t rings)
		{
			std::vector<ProfilePoint> profile;
			const float radius = 0.5f;
			const float halfCylinder = 0.5f;
			for(uint32_t r = 0; r <= rings; r++) // top hemisphere
			{
				float phi = static_cast<float>(r) / rings * glm::half_pi<float>();
				profile.push_back({ std::sin(phi) * radius, halfCylinder + std::cos(phi) * radius, std::sin(phi), std::cos(phi), 0.0f });
			}
			for(uint32_t r = 0; r <= rings; r++) // bottom hemisphere
			{
				float phi = glm::half_pi<float>() + static_cast<float>(r) / rings * glm::half_pi<float>();
				profile.push_back({ std::sin(phi) * radius, -halfCylinder + std::cos(phi) * radius, std::sin(phi), std::cos(phi), 1.0f });
			}
			float total = static_cast<float>(profile.size() - 1);
			for(size_t k = 0; k < profile.size(); k++)
				profile[k].V = static_cast<float>(k) / total;
			return Revolve("builtin://Capsule", profile, segments);
		}

		Ref<Mesh> CreateCone(uint32_t segments)
		{
			glm::vec2 n = glm::normalize(glm::vec2(1.0f, 0.5f)); // radial, y
			auto side = Revolve("side", { { 0.0f, 0.5f, n.x, n.y, 0 }, { 0.5f, -0.5f, n.x, n.y, 1 } }, segments);
			std::vector<Vertex> verts = side->GetVertices();
			std::vector<uint32_t> idx = side->GetIndices();
			uint32_t center = static_cast<uint32_t>(verts.size());
			verts.push_back({ { 0, -0.5f, 0 }, { 0, -1, 0 }, { 1, 0, 0, 1 }, { 0.5f, 0.5f } });
			for(uint32_t s = 0; s <= segments; s++)
			{
				float a = static_cast<float>(s) / segments * glm::two_pi<float>();
				verts.push_back({ { 0.5f * std::cos(a), -0.5f, 0.5f * std::sin(a) }, { 0, -1, 0 }, { 1, 0, 0, 1 }, { 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a) } });
			}
			for(uint32_t s = 0; s < segments; s++)
				idx.insert(idx.end(), { center, center + 1 + s, center + 2 + s });
			return CreateRef<Mesh>("builtin://Cone", std::move(verts), std::move(idx));
		}

	}

}
