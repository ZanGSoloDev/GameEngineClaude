#pragma once

#include "EditorContext.h"

namespace StarfallEditor::Actions {

	// All creation functions add the entity under `parent` (if valid), select it and commit an undo step.
	Entity CreateEmpty(EditorContext& ctx, Entity parent = {});
	Entity CreateMesh(EditorContext& ctx, const std::string& name, const std::string& mesh, Entity parent = {});
	Entity CreateLight(EditorContext& ctx, LightType type, Entity parent = {});
	Entity CreateCamera(EditorContext& ctx, Entity parent = {});
	Entity CreateAudioSource(EditorContext& ctx, Entity parent = {});

	Entity Duplicate(EditorContext& ctx, Entity entity);
	void Delete(EditorContext& ctx, Entity entity);
	// Re-parents; `newParent` may be invalid to move to the root. Keeps the world transform.
	void Reparent(EditorContext& ctx, Entity child, Entity newParent);

	// Instantiates an asset dropped/double clicked (prefab, model) at the origin or under `parent`.
	Entity Instantiate(EditorContext& ctx, const AssetPath& asset, Entity parent = {});

	// Saves the entity subtree as a prefab asset. Returns the asset path (empty on failure).
	AssetPath SaveAsPrefab(EditorContext& ctx, Entity entity);

	// Applies a dropped asset (material, mesh, script, audio clip) to the entity. Returns true if applicable.
	bool AssignAsset(EditorContext& ctx, Entity entity, const AssetPath& asset);

	// Bounding sphere of the entity and its renderable descendants (world space).
	void GetBounds(Scene& scene, Entity entity, glm::vec3& center, float& radius);

	// Name that does not collide with siblings ("Cube", "Cube (1)", ...).
	std::string UniqueName(Scene& scene, const std::string& base);

}
