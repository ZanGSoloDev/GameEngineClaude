#include <doctest/doctest.h>

#include "Starfall/Renderer/AssetManager.h"
#include "Starfall/Renderer/Material.h"
#include "Starfall/Renderer/Mesh.h"
#include "Starfall/Renderer/Model.h"
#include "Starfall/Renderer/Texture.h"
#include "Starfall/Scene/Entity.h"

#include "TestUtils.h"

#include <cstring>

using namespace Starfall;

namespace {

	// Checks structural validity, outward winding and unit normals of a closed, origin-centered convex-ish mesh.
	void CheckMesh(const Ref<Mesh>& mesh, bool convexOutward)
	{
		REQUIRE(mesh);
		const auto& v = mesh->GetVertices();
		const auto& idx = mesh->GetIndices();
		CHECK(v.size() >= 3);
		CHECK(idx.size() >= 3);
		CHECK(idx.size() % 3 == 0);
		for(uint32_t i : idx)
			REQUIRE(i < v.size());
		for(const Vertex& vert : v)
		{
			CHECK(glm::length(vert.Normal) == doctest::Approx(1.0f).epsilon(0.01));
			CHECK(glm::dot(glm::vec3(vert.Tangent), vert.Normal) == doctest::Approx(0.0f).epsilon(0.05));
			CHECK((vert.Tangent.w == 1.0f || vert.Tangent.w == -1.0f));
		}
		CHECK(mesh->GetBounds().IsValid());

		if(convexOutward)
		{
			int wrong = 0;
			for(size_t i = 0; i < idx.size(); i += 3)
			{
				glm::vec3 a = v[idx[i]].Position, b = v[idx[i + 1]].Position, c = v[idx[i + 2]].Position;
				glm::vec3 faceNormal = glm::cross(b - a, c - a);
				if(glm::dot(faceNormal, faceNormal) < 1e-12f)
					continue; // degenerate pole triangles
				glm::vec3 centroid = (a + b + c) / 3.0f;
				if(glm::dot(glm::normalize(faceNormal), glm::normalize(centroid)) < 0.0f)
					wrong++;
				// the stored vertex normal must agree with the winding (front face = CCW)
				if(glm::dot(faceNormal, v[idx[i]].Normal) < 0.0f)
					wrong++;
			}
			CHECK(wrong == 0);
		}
	}

	std::string Base64(const std::vector<uint8_t>& data)
	{
		static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		for(size_t i = 0; i < data.size(); i += 3)
		{
			uint32_t n = data[i] << 16;
			if(i + 1 < data.size()) n |= data[i + 1] << 8;
			if(i + 2 < data.size()) n |= data[i + 2];
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += i + 1 < data.size() ? table[(n >> 6) & 63] : '=';
			out += i + 2 < data.size() ? table[n & 63] : '=';
		}
		return out;
	}

	// A single triangle (no normals/uv -> importer must generate them) with one material, in a node with a transform.
	std::string TriangleGltf(const std::string& imageUri = "")
	{
		std::vector<uint8_t> buffer(3 * 12 + 8);
		float positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
		uint16_t indices[3] = { 0, 1, 2 };
		std::memcpy(buffer.data(), positions, sizeof(positions));
		std::memcpy(buffer.data() + 36, indices, sizeof(indices));

		std::string textureBlock;
		std::string materialExtra;
		if(!imageUri.empty())
		{
			textureBlock = R"(,"images":[{"uri":")" + imageUri + R"("}],"textures":[{"source":0}])";
			materialExtra = R"(,"baseColorTexture":{"index":0})";
		}
		return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
			"nodes":[{"name":"Tri","mesh":0,"translation":[1,2,3],"scale":[2,2,2],"children":[1]},{"name":"Child"}],
			"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
			"materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0.25,"roughnessFactor":0.75)" + materialExtra + R"(},"doubleSided":true,"alphaMode":"MASK","alphaCutoff":0.3}]
			)" + textureBlock + R"(,
			"buffers":[{"byteLength":44,"uri":"data:application/octet-stream;base64,)" + Base64(buffer) + R"("}],
			"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
			"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
	}

}

TEST_CASE("Procedural meshes are well formed")
{
	CheckMesh(MeshFactory::CreateCube(), true);
	CheckMesh(MeshFactory::CreateSphere(), true);
	CheckMesh(MeshFactory::CreateCylinder(), true);
	CheckMesh(MeshFactory::CreateCapsule(), true);
	CheckMesh(MeshFactory::CreateCone(), false);
	CheckMesh(MeshFactory::CreatePlane(), false);
	CheckMesh(MeshFactory::CreateQuad(), false);

	auto cube = MeshFactory::CreateCube();
	CHECK(cube->GetVertices().size() == 24);
	CHECK(cube->GetIndexCount() == 36);
	CHECK(cube->GetBounds().Min == glm::vec3(-0.5f));
	CHECK(cube->GetBounds().Max == glm::vec3(0.5f));
	auto capsule = MeshFactory::CreateCapsule();
	CHECK(capsule->GetBounds().Max.y == doctest::Approx(1.0f).epsilon(0.001));
	CHECK(capsule->GetBounds().Max.x == doctest::Approx(0.5f).epsilon(0.001));
	auto plane = MeshFactory::CreatePlane();
	for(const auto& vert : plane->GetVertices())
		CHECK(vert.Normal == glm::vec3(0, 1, 0));
}

TEST_CASE("Mesh raycast")
{
	auto cube = MeshFactory::CreateCube();
	float t = 0;
	CHECK(cube->Raycast({ { 0, 0, 5 }, { 0, 0, -1 } }, t));
	CHECK(t == doctest::Approx(4.5f));
	CHECK_FALSE(cube->Raycast({ { 2, 0, 5 }, { 0, 0, -1 } }, t));
	CHECK_FALSE(cube->Raycast({ { 0, 0, 5 }, { 0, 0, 1 } }, t));
	auto sphere = MeshFactory::CreateSphere();
	CHECK(sphere->Raycast({ { 0, 5, 0 }, { 0, -1, 0 } }, t));
	CHECK(t == doctest::Approx(4.5f).epsilon(0.01));
}

TEST_CASE("Tangent generation follows UV direction")
{
	std::vector<Vertex> v(3);
	v[0].Position = { 0, 0, 0 }; v[0].UV = { 0, 0 };
	v[1].Position = { 1, 0, 0 }; v[1].UV = { 1, 0 };
	v[2].Position = { 0, 1, 0 }; v[2].UV = { 0, 1 };
	for(auto& vert : v) vert.Normal = { 0, 0, 1 };
	Mesh mesh("tri", v, { 0, 1, 2 });
	mesh.RecalculateTangents();
	for(const auto& vert : mesh.GetVertices())
	{
		CHECK(vert.Tangent.x == doctest::Approx(1.0f));
		CHECK(vert.Tangent.w == 1.0f);
	}
	// Mirrored UVs flip handedness
	v[1].UV = { 0, 0 }; v[0].UV = { 1, 0 };
	Mesh mirrored("tri", v, { 0, 1, 2 });
	mirrored.RecalculateTangents();
	CHECK(mirrored.GetVertices()[0].Tangent.w == -1.0f);

	// Degenerate UVs fall back to a valid tangent
	std::vector<Vertex> d(3);
	for(auto& vert : d) vert.Normal = { 0, 1, 0 };
	d[1].Position = { 1, 0, 0 }; d[2].Position = { 0, 0, 1 };
	Mesh degenerate("d", d, { 0, 1, 2 });
	degenerate.RecalculateTangents();
	CHECK(glm::length(glm::vec3(degenerate.GetVertices()[0].Tangent)) == doctest::Approx(1.0f));
}

TEST_CASE("Mip chain")
{
	CHECK(MipChain::Count(1, 1) == 1);
	CHECK(MipChain::Count(256, 256) == 9);
	CHECK(MipChain::Count(256, 64) == 9);
	CHECK(MipChain::Count(5, 3) == 3);

	std::vector<uint8_t> px = { 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255 };
	auto linear = MipChain::Downsample(px, 2, 2, false);
	REQUIRE(linear.size() == 4);
	CHECK(std::abs(int(linear[0]) - 128) <= 1);
	CHECK(linear[3] == 255);
	auto srgb = MipChain::Downsample(px, 2, 2, true);
	CHECK(srgb[0] > 180); // averaging in linear light is brighter than the naive 128
	CHECK(srgb[0] < 200);

	// odd sizes do not read out of bounds
	std::vector<uint8_t> odd(3 * 5 * 4, 200);
	auto result = MipChain::Downsample(odd, 3, 5, false);
	CHECK(result.size() == 1 * 2 * 4);
}

TEST_CASE("Texture2D creation validates input and builds mips")
{
	CHECK_FALSE(Texture2D::CreateFromPixels("bad", 4, 4, std::vector<uint8_t>(10), false));
	CHECK_FALSE(Texture2D::CreateFromPixels("bad", 0, 4, {}, false));
	auto tex = Texture2D::CreateFromPixels("ok", 16, 8, std::vector<uint8_t>(16 * 8 * 4, 128), true);
	REQUIRE(tex);
	CHECK(tex->GetMipCount() == 5);
	CHECK(tex->IsSRGB());
	CHECK(tex->GetWidth() == 16);
	CHECK(tex->HasCPUData());
	auto single = Texture2D::CreateFromPixels("one", 4, 4, std::vector<uint8_t>(64, 1), false, false);
	CHECK(single->GetMipCount() == 1);
}

TEST_CASE("Image encode/decode round trip and corrupt data handling")
{
	TestProject project;
	std::vector<uint8_t> pixels(4 * 2 * 4);
	for(size_t i = 0; i < pixels.size(); i++)
		pixels[i] = static_cast<uint8_t>(i * 7);
	auto file = project.Assets() / "round.png";
	REQUIRE(ImageIO::SavePNG(file, 4, 2, pixels.data()));
	ImageData decoded;
	REQUIRE(ImageIO::LoadLDR(file, decoded));
	CHECK(decoded.Width == 4);
	CHECK(decoded.Height == 2);
	CHECK(decoded.Pixels == pixels);

	ImageData bad;
	CHECK_FALSE(ImageIO::DecodeLDR(nullptr, 0, bad));
	uint8_t junk[16] = { 1, 2, 3 };
	CHECK_FALSE(ImageIO::DecodeLDR(junk, sizeof(junk), bad));
	CHECK_FALSE(ImageIO::LoadLDR(project.Assets() / "missing.png", bad));
	HdrImageData hdr;
	CHECK_FALSE(ImageIO::DecodeHDR(junk, sizeof(junk), hdr));
	CHECK(Texture2D::LoadFromFile(file, true) != nullptr);
	CHECK(Texture2D::LoadFromFile(project.Assets() / "missing.png", true) == nullptr);
}

TEST_CASE("Radiance HDR decoding")
{
	// Minimal flat (non-RLE) .hdr file: 2x1 pixels.
	std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
	std::vector<uint8_t> bytes(header.begin(), header.end());
	for(uint8_t b : { 128, 128, 128, 129, 64, 64, 64, 128 })
		bytes.push_back(b);
	HdrImageData img;
	REQUIRE(ImageIO::DecodeHDR(bytes.data(), bytes.size(), img));
	CHECK(img.Width == 2);
	CHECK(img.Height == 1);
	CHECK(img.Pixels.size() == 8);
	CHECK(img.Pixels[0] == doctest::Approx(1.0f).epsilon(0.02));
	CHECK(img.Pixels[3] == 1.0f);
}

TEST_CASE("Material serialization round trip")
{
	TestProject project;
	ImageData unused;
	std::vector<uint8_t> px(4 * 4 * 4, 255);
	ImageIO::SavePNG(project.Assets() / "Textures" / "albedo.png", 4, 4, px.data());

	Material m("Test");
	auto& d = m.GetData();
	d.BaseColor = { 0.1f, 0.2f, 0.3f, 0.4f };
	d.Metallic = 0.9f;
	d.Roughness = 0.1f;
	d.Emissive = { 1, 2, 3 };
	d.EmissiveIntensity = 4.0f;
	d.Alpha = AlphaMode::Blend;
	d.DoubleSided = true;
	d.UVScale = { 2, 3 };
	m.SetTexturePath(TextureSlot::BaseColor, "Textures/albedo.png");
	std::string text = MaterialSerializer::ToString(m);

	Ref<Material> loaded = MaterialSerializer::FromString(text, "Loaded");
	REQUIRE(loaded);
	const auto& l = loaded->GetData();
	CHECK(l.BaseColor.y == 0.2f);
	CHECK(l.Metallic == 0.9f);
	CHECK(l.Emissive.z == 3.0f);
	CHECK(l.EmissiveIntensity == 4.0f);
	CHECK(l.Alpha == AlphaMode::Blend);
	CHECK(l.DoubleSided);
	CHECK(l.UVScale.y == 3.0f);
	CHECK(loaded->GetTexturePath(TextureSlot::BaseColor) == "Textures/albedo.png");
	CHECK(loaded->GetTexture(TextureSlot::BaseColor) != nullptr);
	CHECK(loaded->GetTexture(TextureSlot::Normal) == nullptr);

	// bad input
	CHECK_FALSE(MaterialSerializer::FromString("garbage", "x"));
	CHECK_FALSE(MaterialSerializer::FromString("{\"Type\":\"Scene\"}", "x"));
	auto clamped = MaterialSerializer::FromString(R"({"Type":"Material","Metallic":7,"Roughness":-3,"AlphaMode":9,"BaseColor":[1,"a",2,3]})", "x");
	REQUIRE(clamped);
	CHECK(clamped->GetData().Metallic == 1.0f);
	CHECK(clamped->GetData().Roughness == 0.0f);
	CHECK(clamped->GetData().Alpha == AlphaMode::Opaque);
	CHECK(clamped->GetData().BaseColor == glm::vec4(1.0f)); // malformed colour keeps the default

	REQUIRE(MaterialSerializer::Save(m, project.Assets() / "Materials" / "m.sfmat"));
	AssetManager::Clear();
	auto fromDisk = AssetManager::GetMaterial("Materials/m.sfmat");
	REQUIRE(fromDisk);
	CHECK(fromDisk->GetData().Metallic == 0.9f);
	CHECK(AssetManager::GetMaterial("Materials/m.sfmat") == fromDisk); // cached
	CHECK(AssetManager::GetMaterial("") == AssetManager::GetDefaultMaterial());
	CHECK(AssetManager::GetMaterial("Materials/none.sfmat") == nullptr);
	AssetManager::Clear();
}

TEST_CASE("AssetManager builtin meshes, caching and invalidation")
{
	AssetManager::Clear();
	auto cube = AssetManager::GetMesh("builtin://Cube");
	REQUIRE(cube);
	CHECK(AssetManager::GetMesh("builtin://Cube") == cube);
	CHECK(cube->GetDefaultMaterial() != nullptr);
	for(const std::string& name : AssetManager::GetBuiltinMeshNames())
		CHECK_MESSAGE(AssetManager::GetMesh("builtin://" + name) != nullptr, name);
	CHECK(AssetManager::GetMesh("builtin://Nope") == nullptr);
	CHECK(AssetManager::GetMesh("nonsense") == nullptr);
	CHECK(AssetManager::GetMesh("Models/none.gltf#0") == nullptr);
	CHECK(AssetManager::GetMesh("Models/none.gltf#abc") == nullptr);
	CHECK(AssetManager::GetMesh("") == nullptr);
	CHECK(AssetManager::IsBuiltinMesh("builtin://Cube"));
	CHECK_FALSE(AssetManager::IsBuiltinMesh("Models/a.gltf#0"));
	AssetManager::Invalidate("builtin://Cube");
	CHECK(AssetManager::GetMesh("builtin://Cube") != cube);
	CHECK(AssetManager::GetWhiteTexture()->GetWidth() == 1);
	CHECK(AssetManager::GetFlatNormalTexture()->GetPixels()[2] == 255);
	AssetManager::Clear();
}

TEST_CASE("glTF import: geometry, material, hierarchy, generated normals")
{
	TestProject project;
	project.Write("Models/Tri.gltf", TriangleGltf());
	AssetManager::Clear();

	Ref<Model> model = AssetManager::GetModel("Models/Tri.gltf");
	REQUIRE(model);
	REQUIRE(model->Meshes.size() == 1);
	REQUIRE(model->Materials.size() == 1);
	REQUIRE(model->Nodes.size() == 2);
	CHECK(model->RootNodes == std::vector<int>{ 0 });
	CHECK(model->Nodes[0].Translation == glm::vec3(1, 2, 3));
	CHECK(model->Nodes[0].Scale == glm::vec3(2, 2, 2));
	CHECK(model->Nodes[0].Children == std::vector<int>{ 1 });
	CHECK(model->Nodes[1].Parent == 0);
	CHECK(model->Nodes[0].Meshes.size() == 1);

	const Ref<Mesh>& mesh = model->Meshes[0];
	CHECK(mesh->GetVertices().size() == 3);
	CHECK(mesh->GetIndexCount() == 3);
	CHECK(mesh->GetVertices()[0].Normal.z == doctest::Approx(1.0f)); // generated, CCW triangle faces +Z
	CHECK(glm::length(glm::vec3(mesh->GetVertices()[0].Tangent)) == doctest::Approx(1.0f));
	CHECK(mesh->GetDefaultMaterial() == model->Materials[0]);

	const MaterialData& m = model->Materials[0]->GetData();
	CHECK(m.Metallic == 0.25f);
	CHECK(m.Roughness == 0.75f);
	CHECK(m.BaseColor.r == 1.0f);
	CHECK(m.DoubleSided);
	CHECK(m.Alpha == AlphaMode::Mask);
	CHECK(m.AlphaCutoff == doctest::Approx(0.3f));

	CHECK(AssetManager::GetMesh("Models/Tri.gltf#0") == mesh);
	CHECK(AssetManager::GetMesh("Models/Tri.gltf#1") == nullptr);
	AssetManager::Clear();
}

TEST_CASE("glTF import: external textures resolve, escapes and corrupt files are rejected")
{
	TestProject project;
	std::vector<uint8_t> px(8 * 8 * 4, 200);
	ImageIO::SavePNG(project.Assets() / "Models" / "tex.png", 8, 8, px.data());
	ImageIO::SavePNG(project.Assets() / "Secret.png", 8, 8, px.data());
	project.Write("Models/WithTex.gltf", TriangleGltf("tex.png"));
	project.Write("Models/Escape.gltf", TriangleGltf("../Secret.png"));
	project.Write("Models/Missing.gltf", TriangleGltf("nope.png"));
	project.Write("Models/Corrupt.gltf", "{ this is not gltf");
	project.Write("Models/Empty.gltf", R"({"asset":{"version":"2.0"}})");
	AssetManager::Clear();

	auto withTex = AssetManager::GetModel("Models/WithTex.gltf");
	REQUIRE(withTex);
	auto base = withTex->Materials[0]->GetTexture(TextureSlot::BaseColor);
	REQUIRE(base);
	CHECK(base->GetWidth() == 8);
	CHECK(base->IsSRGB());

	auto escape = AssetManager::GetModel("Models/Escape.gltf");
	REQUIRE(escape); // model loads, but the texture outside the directory is ignored
	CHECK(escape->Materials[0]->GetTexture(TextureSlot::BaseColor) == nullptr);

	auto missing = AssetManager::GetModel("Models/Missing.gltf");
	REQUIRE(missing);
	CHECK(missing->Materials[0]->GetTexture(TextureSlot::BaseColor) == nullptr);

	CHECK(AssetManager::GetModel("Models/Corrupt.gltf") == nullptr);
	CHECK(AssetManager::GetModel("Models/Empty.gltf") == nullptr);
	CHECK(AssetManager::GetModel("Models/DoesNotExist.gltf") == nullptr);
	CHECK(AssetManager::GetModel("../Escape.gltf") == nullptr);
	AssetManager::Clear();
}

TEST_CASE("InstantiateModel builds an entity hierarchy referencing model meshes")
{
	TestProject project;
	project.Write("Models/Tri.gltf", TriangleGltf());
	AssetManager::Clear();
	Scene scene;
	Entity root = InstantiateModel(scene, "Models/Tri.gltf", Entity());
	REQUIRE(root);
	CHECK(root.GetName() == "Tri");
	REQUIRE(root.GetChildren().size() == 1);
	Entity node = root.GetChildren()[0];
	CHECK(node.GetName() == "Tri");
	CHECK(node.Transform().Translation == glm::vec3(1, 2, 3));
	REQUIRE(node.HasComponent<MeshRendererComponent>());
	CHECK(node.GetComponent<MeshRendererComponent>().Mesh == "Models/Tri.gltf#0");
	REQUIRE(node.GetChildren().size() == 1);
	CHECK(node.GetChildren()[0].GetName() == "Child");
	CHECK(scene.GetEntityCount() == 3);
	CHECK(AssetManager::GetMesh(node.GetComponent<MeshRendererComponent>().Mesh) != nullptr);

	Entity parent = scene.CreateEntity("P");
	Entity second = InstantiateModel(scene, "Models/Tri.gltf", parent);
	CHECK(second.GetParent() == parent);
	CHECK_FALSE(InstantiateModel(scene, "Models/none.gltf", Entity()));
	AssetManager::Clear();
}
