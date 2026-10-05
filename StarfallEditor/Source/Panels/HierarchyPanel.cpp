#include "HierarchyPanel.h"

#include "../AssetFiles.h"
#include "../EditorActions.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace StarfallEditor {

	namespace {

		bool ContainsIgnoreCase(const std::string& text, const char* needle)
		{
			if(!*needle)
				return true;
			std::string n(needle);
			auto it = std::search(text.begin(), text.end(), n.begin(), n.end(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			return it != text.end();
		}

		ImU32 EntityColor(Entity e)
		{
			if(e.HasComponent<CameraComponent>()) return IM_COL32(120, 220, 140, 255);
			if(e.HasComponent<LightComponent>()) return IM_COL32(255, 220, 100, 255);
			if(e.HasComponent<MeshRendererComponent>()) return IM_COL32(120, 170, 255, 255);
			if(e.HasComponent<AudioSourceComponent>()) return IM_COL32(200, 140, 255, 255);
			if(e.HasComponent<ColliderComponent>()) return IM_COL32(255, 140, 120, 255);
			return IM_COL32(170, 170, 175, 255);
		}

	}

	bool HierarchyPanel::PassesFilter(Entity entity) const
	{
		if(!m_Filter[0])
			return true;
		if(ContainsIgnoreCase(entity.GetName(), m_Filter))
			return true;
		for(Entity child : entity.GetChildren())
			if(PassesFilter(child))
				return true;
		return false;
	}

	void HierarchyPanel::DrawCreateMenu(EditorContext& ctx, Entity parent)
	{
		if(ImGui::MenuItem("Create Empty"))
			Actions::CreateEmpty(ctx, parent);
		if(ImGui::BeginMenu("3D Object"))
		{
			for(const char* name : { "Cube", "Sphere", "Plane", "Quad", "Cylinder", "Capsule", "Cone" })
				if(ImGui::MenuItem(name))
					Actions::CreateMesh(ctx, name, std::string("builtin://") + name, parent);
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Light"))
		{
			if(ImGui::MenuItem("Directional Light")) Actions::CreateLight(ctx, LightType::Directional, parent);
			if(ImGui::MenuItem("Point Light")) Actions::CreateLight(ctx, LightType::Point, parent);
			if(ImGui::MenuItem("Spot Light")) Actions::CreateLight(ctx, LightType::Spot, parent);
			ImGui::EndMenu();
		}
		if(ImGui::MenuItem("Camera"))
			Actions::CreateCamera(ctx, parent);
		if(ImGui::MenuItem("Audio Source"))
			Actions::CreateAudioSource(ctx, parent);
	}

	void HierarchyPanel::DrawNode(EditorContext& ctx, Entity entity)
	{
		if(!PassesFilter(entity))
			return;

		std::vector<Entity> children = entity.GetChildren();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
		if(children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if(ctx.Selected == entity.GetUUID())
			flags |= ImGuiTreeNodeFlags_Selected;
		if(m_Filter[0])
			ImGui::SetNextItemOpen(true);

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		bool renaming = m_Renaming == entity.GetUUID();
		bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint64_t>(entity.GetUUID()))), flags, "%s", renaming ? "" : "      ");
		ImGui::PopStyleColor();
		const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();

		// small colored type marker + name drawn over the node
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float lineHeight = ImGui::GetFrameHeight();
		float textX = cursor.x + ImGui::GetTreeNodeToLabelSpacing();
		dl->AddRectFilled(ImVec2(textX + 2, cursor.y + lineHeight * 0.5f - 4), ImVec2(textX + 10, cursor.y + lineHeight * 0.5f + 4), EntityColor(entity), 2.0f);
		if(!renaming)
			dl->AddText(ImVec2(textX + 16, cursor.y + (lineHeight - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), entity.GetName().c_str());

		if(clicked)
			ctx.Select(entity);
		if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ctx.FocusEntity)
			ctx.FocusEntity(entity);

		if(renaming)
		{
			ImGui::SameLine(ImGui::GetTreeNodeToLabelSpacing() + 18);
			ImGui::SetNextItemWidth(-1);
			if(m_FocusRename)
			{
				ImGui::SetKeyboardFocusHere();
				m_FocusRename = false;
			}
			if(ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				if(m_RenameBuffer[0])
				{
					entity.GetComponent<TagComponent>().Tag = m_RenameBuffer;
					if(ctx.Commit)
						ctx.Commit("Rename");
				}
				m_Renaming = UUID(0);
			}
			else if(ImGui::IsItemDeactivated())
				m_Renaming = UUID(0);
		}

		// drag & drop
		if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			uint64_t id = entity.GetUUID();
			ImGui::SetDragDropPayload("ENTITY", &id, sizeof(id));
			ImGui::TextUnformatted(entity.GetName().c_str());
			ImGui::EndDragDropSource();
		}
		if(ImGui::BeginDragDropTarget())
		{
			if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
				m_Reparents.push_back({ UUID(*static_cast<const uint64_t*>(payload->Data)), entity.GetUUID() });
			if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
				m_AssetDrops.emplace_back(entity.GetUUID(), std::string(static_cast<const char*>(payload->Data)));
			ImGui::EndDragDropTarget();
		}

		if(ImGui::BeginPopupContextItem())
		{
			ctx.Select(entity);
			if(ImGui::MenuItem("Rename", "F2"))
			{
				m_Renaming = entity.GetUUID();
				std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", entity.GetName().c_str());
				m_FocusRename = true;
			}
			if(ImGui::MenuItem("Duplicate", "Ctrl+D"))
				m_PendingDuplicate = entity.GetUUID();
			if(ImGui::MenuItem("Delete", "Del"))
				m_PendingDelete = entity.GetUUID();
			if(ImGui::MenuItem("Focus", "F") && ctx.FocusEntity)
				ctx.FocusEntity(entity);
			if(ImGui::MenuItem("Save As Prefab"))
			{
				AssetPath saved = Actions::SaveAsPrefab(ctx, entity);
				if(ctx.ShowToast)
					ctx.ShowToast(saved.empty() ? "Could not save prefab" : "Saved prefab " + saved);
			}
			if(entity.GetParent() && ImGui::MenuItem("Unparent"))
				m_Reparents.push_back({ entity.GetUUID(), UUID(0) });
			ImGui::Separator();
			if(ImGui::BeginMenu("Create Child"))
			{
				DrawCreateMenu(ctx, entity);
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		if(opened)
		{
			for(Entity child : children)
				DrawNode(ctx, child);
			ImGui::TreePop();
		}
	}

	void HierarchyPanel::OnImGui(EditorContext& ctx, bool* open)
	{
		if(!ImGui::Begin("Hierarchy", open))
		{
			ImGui::End();
			return;
		}

		ImGui::SetNextItemWidth(-60.0f);
		ImGui::InputTextWithHint("##filter", "Search...", m_Filter, sizeof(m_Filter));
		ImGui::SameLine();
		if(ImGui::Button("+ Add"))
			ImGui::OpenPopup("CreateEntityPopup");
		if(ImGui::BeginPopup("CreateEntityPopup"))
		{
			DrawCreateMenu(ctx, {});
			ImGui::EndPopup();
		}
		ImGui::Separator();

		Scene& scene = ctx.GetScene();
		ImGui::BeginChild("##tree", ImVec2(0, 0), false);
		for(Entity root : scene.GetRootEntities())
			DrawNode(ctx, root);

		// Empty space: drop target (unparent) and context menu
		ImVec2 remaining = ImGui::GetContentRegionAvail();
		if(remaining.y > 8.0f)
		{
			ImGui::InvisibleButton("##empty", ImVec2(-1, remaining.y));
			if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
				ctx.Selected = UUID(0);
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
					m_Reparents.push_back({ UUID(*static_cast<const uint64_t*>(payload->Data)), UUID(0) });
				if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
				{
					AssetPath asset(static_cast<const char*>(payload->Data));
					AssetKind kind = ClassifyAsset(asset, false);
					if(kind == AssetKind::Prefab || kind == AssetKind::Model)
						Actions::Instantiate(ctx, asset);
				}
				ImGui::EndDragDropTarget();
			}
			if(ImGui::BeginPopupContextItem("##emptyctx"))
			{
				DrawCreateMenu(ctx, {});
				ImGui::EndPopup();
			}
		}
		ImGui::EndChild();

		// Keyboard shortcuts while the panel is focused
		if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
		{
			if(ImGui::IsKeyPressed(ImGuiKey_Delete))
				m_PendingDelete = ctx.Selected;
			if(ImGui::IsKeyPressed(ImGuiKey_D) && ImGui::GetIO().KeyCtrl)
				m_PendingDuplicate = ctx.Selected;
			if(ImGui::IsKeyPressed(ImGuiKey_F2))
				if(Entity selected = ctx.GetSelectedEntity())
				{
					m_Renaming = selected.GetUUID();
					std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", selected.GetName().c_str());
					m_FocusRename = true;
				}
		}
		ImGui::End();

		// Apply deferred operations.
		for(const PendingReparent& op : m_Reparents)
		{
			Entity child = scene.FindEntityByUUID(op.Child);
			Entity parent = scene.FindEntityByUUID(op.Parent);
			Actions::Reparent(ctx, child, parent);
		}
		m_Reparents.clear();
		for(const auto& [target, asset] : m_AssetDrops)
		{
			Entity entity = scene.FindEntityByUUID(target);
			AssetKind kind = ClassifyAsset(asset, false);
			if(kind == AssetKind::Prefab)
				Actions::Instantiate(ctx, asset, entity);
			else
				Actions::AssignAsset(ctx, entity, asset);
		}
		m_AssetDrops.clear();
		if(!m_PendingDuplicate.IsNull())
		{
			Actions::Duplicate(ctx, scene.FindEntityByUUID(m_PendingDuplicate));
			m_PendingDuplicate = UUID(0);
		}
		if(!m_PendingDelete.IsNull())
		{
			Actions::Delete(ctx, scene.FindEntityByUUID(m_PendingDelete));
			m_PendingDelete = UUID(0);
		}
	}

}
