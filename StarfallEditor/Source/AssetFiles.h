#pragma once

#include "Starfall/Scene/Components.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace StarfallEditor {

	enum class AssetKind { Folder, Scene, Prefab, Model, Material, Texture, Hdri, Audio, Script, Project, Other };

	AssetKind ClassifyAsset(const std::filesystem::path& path, bool isDirectory);
	const char* AssetKindName(AssetKind kind);

	// Draws a vector icon for the asset kind inside the rectangle (no image assets needed).
	void DrawAssetIcon(ImDrawList* drawList, AssetKind kind, const ImVec2& min, const ImVec2& max);

	// All project assets (recursively) whose extension matches one of `extensions` (lowercase, with dot).
	std::vector<Starfall::AssetPath> ListAssets(const std::vector<std::string>& extensions);

}
