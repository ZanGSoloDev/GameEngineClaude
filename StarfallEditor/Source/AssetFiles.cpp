#include "AssetFiles.h"

#include "Starfall/Project/Project.h"

#include <algorithm>
#include <cctype>

namespace StarfallEditor {

	AssetKind ClassifyAsset(const std::filesystem::path& path, bool isDirectory)
	{
		if(isDirectory)
			return AssetKind::Folder;
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if(ext == ".sfscene") return AssetKind::Scene;
		if(ext == ".sfprefab") return AssetKind::Prefab;
		if(ext == ".gltf" || ext == ".glb") return AssetKind::Model;
		if(ext == ".sfmat") return AssetKind::Material;
		if(ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return AssetKind::Texture;
		if(ext == ".hdr") return AssetKind::Hdri;
		if(ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return AssetKind::Audio;
		if(ext == ".lua") return AssetKind::Script;
		if(ext == ".sfproj") return AssetKind::Project;
		return AssetKind::Other;
	}

	const char* AssetKindName(AssetKind kind)
	{
		switch(kind)
		{
			case AssetKind::Folder: return "Folder";
			case AssetKind::Scene: return "Scene";
			case AssetKind::Prefab: return "Prefab";
			case AssetKind::Model: return "Model";
			case AssetKind::Material: return "Material";
			case AssetKind::Texture: return "Texture";
			case AssetKind::Hdri: return "HDRI";
			case AssetKind::Audio: return "Audio";
			case AssetKind::Script: return "Script";
			case AssetKind::Project: return "Project";
			case AssetKind::Other: return "File";
		}
		return "File";
	}

	void DrawAssetIcon(ImDrawList* dl, AssetKind kind, const ImVec2& min, const ImVec2& max)
	{
		const float w = max.x - min.x;
		const float h = max.y - min.y;
		auto P = [&](float x, float y) { return ImVec2(min.x + x * w, min.y + y * h); };
		auto color = [](int r, int g, int b) { return IM_COL32(r, g, b, 255); };
		const ImU32 outline = color(20, 22, 28);

		switch(kind)
		{
			case AssetKind::Folder:
			{
				dl->AddRectFilled(P(0.08f, 0.28f), P(0.45f, 0.4f), color(230, 180, 70), 3.0f);
				dl->AddRectFilled(P(0.08f, 0.34f), P(0.92f, 0.82f), color(245, 200, 90), 4.0f);
				dl->AddRect(P(0.08f, 0.34f), P(0.92f, 0.82f), outline, 4.0f);
				return;
			}
			case AssetKind::Scene: // globe-like: framed square with horizon and sun
			{
				dl->AddRectFilled(P(0.12f, 0.15f), P(0.88f, 0.85f), color(70, 120, 200), 6.0f);
				dl->AddRectFilled(P(0.12f, 0.6f), P(0.88f, 0.85f), color(80, 150, 90), 6.0f, ImDrawFlags_RoundCornersBottom);
				dl->AddCircleFilled(P(0.68f, 0.35f), w * 0.1f, color(255, 230, 120));
				dl->AddRect(P(0.12f, 0.15f), P(0.88f, 0.85f), outline, 6.0f, 0, 1.5f);
				return;
			}
			case AssetKind::Prefab: // blue cube
			case AssetKind::Model:
			{
				ImU32 top = kind == AssetKind::Prefab ? color(110, 170, 250) : color(235, 140, 80);
				ImU32 left = kind == AssetKind::Prefab ? color(60, 110, 200) : color(200, 100, 50);
				ImU32 right = kind == AssetKind::Prefab ? color(40, 80, 160) : color(160, 70, 35);
				ImVec2 t[4] = { P(0.5f, 0.12f), P(0.86f, 0.3f), P(0.5f, 0.48f), P(0.14f, 0.3f) };
				ImVec2 l[4] = { P(0.14f, 0.3f), P(0.5f, 0.48f), P(0.5f, 0.88f), P(0.14f, 0.7f) };
				ImVec2 r[4] = { P(0.5f, 0.48f), P(0.86f, 0.3f), P(0.86f, 0.7f), P(0.5f, 0.88f) };
				dl->AddConvexPolyFilled(t, 4, top);
				dl->AddConvexPolyFilled(l, 4, left);
				dl->AddConvexPolyFilled(r, 4, right);
				dl->AddPolyline(t, 4, outline, ImDrawFlags_Closed, 1.2f);
				dl->AddPolyline(l, 4, outline, ImDrawFlags_Closed, 1.2f);
				dl->AddPolyline(r, 4, outline, ImDrawFlags_Closed, 1.2f);
				return;
			}
			case AssetKind::Material: // shaded sphere
			{
				ImVec2 c = P(0.5f, 0.5f);
				float r = w * 0.38f;
				dl->AddCircleFilled(c, r, color(190, 70, 70));
				dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.3f), r * 0.45f, color(240, 130, 130));
				dl->AddCircleFilled(ImVec2(c.x - r * 0.4f, c.y - r * 0.4f), r * 0.15f, color(255, 230, 230));
				dl->AddCircle(c, r, outline, 32, 1.5f);
				return;
			}
			case AssetKind::Texture:
			case AssetKind::Hdri:
			{
				dl->AddRectFilled(P(0.1f, 0.18f), P(0.9f, 0.82f), kind == AssetKind::Hdri ? color(40, 60, 110) : color(235, 235, 240), 4.0f);
				dl->AddTriangleFilled(P(0.2f, 0.76f), P(0.45f, 0.4f), P(0.7f, 0.76f), color(90, 160, 100));
				dl->AddTriangleFilled(P(0.45f, 0.76f), P(0.65f, 0.5f), P(0.84f, 0.76f), color(60, 130, 80));
				dl->AddCircleFilled(P(0.72f, 0.32f), w * 0.07f, kind == AssetKind::Hdri ? color(255, 220, 120) : color(250, 200, 80));
				dl->AddRect(P(0.1f, 0.18f), P(0.9f, 0.82f), outline, 4.0f, 0, 1.5f);
				return;
			}
			case AssetKind::Audio: // speaker + waves
			{
				dl->AddRectFilled(P(0.18f, 0.4f), P(0.36f, 0.6f), color(110, 200, 150));
				ImVec2 cone[4] = { P(0.36f, 0.4f), P(0.58f, 0.22f), P(0.58f, 0.78f), P(0.36f, 0.6f) };
				dl->AddConvexPolyFilled(cone, 4, color(110, 200, 150));
				dl->AddBezierQuadratic(P(0.68f, 0.35f), P(0.82f, 0.5f), P(0.68f, 0.65f), color(160, 230, 190), 2.5f);
				dl->AddBezierQuadratic(P(0.76f, 0.24f), P(0.98f, 0.5f), P(0.76f, 0.76f), color(160, 230, 190), 2.5f);
				return;
			}
			case AssetKind::Script: // page with lua moon
			{
				dl->AddRectFilled(P(0.2f, 0.1f), P(0.8f, 0.9f), color(40, 60, 140), 5.0f);
				dl->AddCircleFilled(P(0.5f, 0.5f), w * 0.2f, color(250, 250, 255));
				dl->AddCircleFilled(P(0.58f, 0.42f), w * 0.15f, color(40, 60, 140));
				dl->AddCircleFilled(P(0.72f, 0.28f), w * 0.05f, color(250, 250, 255));
				dl->AddRect(P(0.2f, 0.1f), P(0.8f, 0.9f), outline, 5.0f, 0, 1.5f);
				return;
			}
			default:
			{
				dl->AddRectFilled(P(0.2f, 0.1f), P(0.8f, 0.9f), color(200, 200, 205), 4.0f);
				ImVec2 fold[3] = { P(0.62f, 0.1f), P(0.8f, 0.28f), P(0.62f, 0.28f) };
				dl->AddConvexPolyFilled(fold, 3, color(150, 150, 160));
				dl->AddRect(P(0.2f, 0.1f), P(0.8f, 0.9f), outline, 4.0f, 0, 1.5f);
				return;
			}
		}
	}

	std::vector<Starfall::AssetPath> ListAssets(const std::vector<std::string>& extensions)
	{
		std::vector<Starfall::AssetPath> result;
		std::filesystem::path root = Starfall::Project::GetAssetDirectory();
		std::error_code ec;
		if(root.empty() || !std::filesystem::is_directory(root, ec))
			return result;
		for(auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if(!it->is_regular_file(ec))
				continue;
			std::string ext = it->path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if(std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
				result.push_back(it->path().lexically_relative(root).generic_string());
		}
		std::sort(result.begin(), result.end());
		return result;
	}

}
