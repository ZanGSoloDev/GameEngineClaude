#include "InspectorPanel.h"

#include "../AssetFiles.h"
#include "Starfall/Audio/AudioWorld.h"
#include "Starfall/Physics/PhysicsWorld.h"
#include "Starfall/Project/Project.h"
#include "Starfall/Renderer/AssetManager.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>

namespace StarfallEditor {

	namespace {

		// Set by the widgets below when an edit finished and an undo step should be recorded.
		bool s_EditFinished = false;

		void Track(bool changed, bool continuous)
		{
			if(continuous)
			{
				if(ImGui::IsItemDeactivatedAfterEdit())
					s_EditFinished = true;
			}
			else if(changed)
			{
				s_EditFinished = true;
			}
		}

		void BeginRow(const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
		}

		bool Float(const char* label, float& v, float speed = 0.05f, float min = 0.0f, float max = 0.0f, const char* fmt = "%.3f")
		{
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = ImGui::DragFloat("##v", &v, speed, min, max, fmt);
			Track(changed, true);
			ImGui::PopID();
			return changed;
		}

		bool Angle(const char* label, float& radians, float min, float max)
		{
			float degrees = glm::degrees(radians);
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = ImGui::DragFloat("##v", &degrees, 0.5f, min, max, "%.1f deg");
			Track(changed, true);
			ImGui::PopID();
			if(changed)
				radians = glm::radians(degrees);
			return changed;
		}

		bool Check(const char* label, bool& v)
		{
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = ImGui::Checkbox("##v", &v);
			Track(changed, false);
			ImGui::PopID();
			return changed;
		}

		bool Combo(const char* label, int& index, const char* const* items, int count)
		{
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = ImGui::Combo("##v", &index, items, count);
			Track(changed, false);
			ImGui::PopID();
			return changed;
		}

		bool Color3(const char* label, glm::vec3& c)
		{
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = ImGui::ColorEdit3("##v", &c.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			Track(changed, true);
			ImGui::PopID();
			return changed;
		}

		bool Vec3(const char* label, glm::vec3& v, float resetValue, float speed = 0.05f)
		{
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = false;
			float full = ImGui::GetContentRegionAvail().x;
			float button = ImGui::GetFrameHeight();
			float widget = (full - 3.0f * button - 2.0f * ImGui::GetStyle().ItemInnerSpacing.x) / 3.0f;
			const ImVec4 colors[3] = { { 0.75f, 0.2f, 0.2f, 1 }, { 0.25f, 0.65f, 0.25f, 1 }, { 0.25f, 0.4f, 0.85f, 1 } };
			const char* names[3] = { "X", "Y", "Z" };
			for(int i = 0; i < 3; i++)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, colors[i]);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[i].x + 0.1f, colors[i].y + 0.1f, colors[i].z + 0.1f, 1));
				if(ImGui::Button(names[i], ImVec2(button, button)))
				{
					v[i] = resetValue;
					changed = true;
					s_EditFinished = true;
				}
				ImGui::PopStyleColor(2);
				ImGui::SameLine(0, 0);
				ImGui::SetNextItemWidth(widget);
				ImGui::PushID(i);
				bool c = ImGui::DragFloat("##c", &v[i], speed, 0.0f, 0.0f, "%.3f");
				Track(c, true);
				changed |= c;
				ImGui::PopID();
				if(i < 2)
					ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
			}
			ImGui::PopID();
			return changed;
		}

		bool Text(const char* label, std::string& value)
		{
			BeginRow(label);
			ImGui::PushID(label);
			char buffer[512];
			std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
			bool changed = ImGui::InputText("##v", buffer, sizeof(buffer));
			Track(changed, true);
			ImGui::PopID();
			if(changed)
				value = buffer;
			return changed;
		}

		// Asset path field: text box + picker popup listing matching project files + drag & drop target.
		bool AssetField(const char* label, AssetPath& path, const std::vector<std::string>& extensions)
		{
			BeginRow(label);
			ImGui::PushID(label);
			bool changed = false;
			float button = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x);
			char buffer[512];
			std::snprintf(buffer, sizeof(buffer), "%s", path.c_str());
			if(ImGui::InputText("##path", buffer, sizeof(buffer)))
			{
				path = buffer;
				changed = true;
			}
			Track(changed, true);
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
				{
					AssetPath dropped(static_cast<const char*>(payload->Data));
					std::string ext = std::filesystem::path(dropped).extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if(std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
					{
						path = dropped;
						changed = true;
						s_EditFinished = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if(ImGui::Button("...", ImVec2(button, button)))
				ImGui::OpenPopup("##picker");
			if(ImGui::BeginPopup("##picker"))
			{
				static std::vector<AssetPath> assets;
				if(ImGui::IsWindowAppearing())
					assets = ListAssets(extensions);
				if(ImGui::Selectable("<None>"))
				{
					path.clear();
					changed = true;
					s_EditFinished = true;
				}
				for(const AssetPath& asset : assets)
					if(ImGui::Selectable(asset.c_str()))
					{
						path = asset;
						changed = true;
						s_EditFinished = true;
					}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			return changed;
		}

		template<typename T, typename Fn>
		bool ComponentHeader(Entity entity, const char* name, Fn&& body, bool removable = true)
		{
			if(!entity.HasComponent<T>())
				return false;
			ImGui::PushID(name);
			bool open = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
			bool remove = false;
			if(removable && ImGui::BeginPopupContextItem("##ctx"))
			{
				if(ImGui::MenuItem("Remove Component"))
					remove = true;
				ImGui::EndPopup();
			}
			if(open)
			{
				if(ImGui::BeginTable("##props", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.38f);
					ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
					body(entity.GetComponent<T>());
					ImGui::EndTable();
				}
			}
			ImGui::PopID();
			if(remove)
			{
				entity.RemoveComponent<T>();
				s_EditFinished = true;
				return true;
			}
			return false;
		}

		const char* const BuiltinMeshes[] = { "builtin://Cube", "builtin://Sphere", "builtin://Plane", "builtin://Quad", "builtin://Cylinder", "builtin://Capsule", "builtin://Cone" };

	}

	void InspectorPanel::DrawMaterialEditor(EditorContext& ctx, const AssetPath& path)
	{
		(void)ctx;
		Ref<Material> material = AssetManager::GetMaterial(path);
		if(!material)
		{
			ImGui::TextDisabled("Material could not be loaded");
			return;
		}
		MaterialData& d = material->GetData();
		bool changed = false;
		if(ImGui::BeginTable("##mat", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.38f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
			glm::vec3 base(d.BaseColor);
			if(Color3("Base Color", base)) { d.BaseColor = glm::vec4(base, d.BaseColor.a); changed = true; }
			changed |= Float("Opacity", d.BaseColor.a, 0.01f, 0.0f, 1.0f);
			changed |= Float("Metallic", d.Metallic, 0.01f, 0.0f, 1.0f);
			changed |= Float("Roughness", d.Roughness, 0.01f, 0.0f, 1.0f);
			changed |= Color3("Emissive", d.Emissive);
			changed |= Float("Emissive Intensity", d.EmissiveIntensity, 0.05f, 0.0f, 100.0f);
			changed |= Float("Normal Scale", d.NormalScale, 0.02f, 0.0f, 4.0f);
			changed |= Float("Occlusion", d.OcclusionStrength, 0.01f, 0.0f, 1.0f);
			int alpha = static_cast<int>(d.Alpha);
			const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
			if(Combo("Alpha Mode", alpha, alphaModes, 3)) { d.Alpha = static_cast<AlphaMode>(alpha); changed = true; }
			if(d.Alpha == AlphaMode::Mask)
				changed |= Float("Alpha Cutoff", d.AlphaCutoff, 0.01f, 0.0f, 1.0f);
			changed |= Check("Double Sided", d.DoubleSided);
			changed |= Float("UV Scale X", d.UVScale.x, 0.05f);
			changed |= Float("UV Scale Y", d.UVScale.y, 0.05f);

			const char* slotNames[] = { "Base Color Map", "Metal/Rough Map", "Normal Map", "Occlusion Map", "Emissive Map" };
			for(int i = 0; i < 5; i++)
			{
				TextureSlot slot = static_cast<TextureSlot>(i);
				AssetPath texturePath = material->GetTexturePath(slot);
				if(AssetField(slotNames[i], texturePath, { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }))
				{
					material->SetTexturePath(slot, texturePath);
					material->SetTexture(slot, AssetManager::GetTexture(texturePath, i == 0 || i == 4));
					changed = true;
				}
			}
			ImGui::EndTable();
		}
		if(changed)
			material->MarkDirty();
		if(ImGui::Button("Save Material"))
			MaterialSerializer::Save(*material, Project::ResolvePath(path));
	}

	void InspectorPanel::DrawAddComponentMenu(Entity entity)
	{
		auto item = [&](const char* name, bool present, auto add) {
			if(ImGui::MenuItem(name, nullptr, false, !present))
			{
				add();
				s_EditFinished = true;
				m_CommitLabel = std::string("Add ") + name;
			}
		};
		item("Camera", entity.HasComponent<CameraComponent>(), [&] { entity.AddComponent<CameraComponent>(); });
		item("Mesh Renderer", entity.HasComponent<MeshRendererComponent>(), [&] { entity.AddComponent<MeshRendererComponent>(); });
		item("Light", entity.HasComponent<LightComponent>(), [&] { entity.AddComponent<LightComponent>(); });
		item("Rigid Body", entity.HasComponent<RigidBodyComponent>(), [&] { entity.AddComponent<RigidBodyComponent>(); });
		item("Collider", entity.HasComponent<ColliderComponent>(), [&] { entity.AddComponent<ColliderComponent>(); });
		item("Audio Source", entity.HasComponent<AudioSourceComponent>(), [&] { entity.AddComponent<AudioSourceComponent>(); });
		item("Audio Listener", entity.HasComponent<AudioListenerComponent>(), [&] { entity.AddComponent<AudioListenerComponent>(); });
		item("Script", entity.HasComponent<ScriptComponent>(), [&] { entity.AddComponent<ScriptComponent>(); });
	}

	void InspectorPanel::DrawEntity(EditorContext& ctx, Entity entity)
	{
		// Name + id
		{
			char name[256];
			std::snprintf(name, sizeof(name), "%s", entity.GetName().c_str());
			ImGui::SetNextItemWidth(-FLT_MIN);
			if(ImGui::InputText("##name", name, sizeof(name)) && name[0])
				entity.GetComponent<TagComponent>().Tag = name;
			if(ImGui::IsItemDeactivatedAfterEdit())
			{
				s_EditFinished = true;
				m_CommitLabel = "Rename";
			}
			ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(static_cast<uint64_t>(entity.GetUUID())));
		}
		ImGui::Separator();

		ComponentHeader<TransformComponent>(entity, "Transform", [&](TransformComponent& t) {
			bool moved = Vec3("Position", t.Translation, 0.0f);
			glm::vec3 degrees = glm::degrees(t.Rotation);
			if(Vec3("Rotation", degrees, 0.0f, 0.5f))
			{
				t.Rotation = glm::radians(degrees);
				moved = true;
			}
			moved |= Vec3("Scale", t.Scale, 1.0f);
			if(moved && ctx.IsPlaying())
				if(PhysicsWorld* physics = ctx.GetScene().GetPhysicsWorld())
					physics->Teleport(entity);
		}, false);

		ComponentHeader<CameraComponent>(entity, "Camera", [&](CameraComponent& c) {
			int projection = static_cast<int>(c.Projection);
			const char* modes[] = { "Perspective", "Orthographic" };
			if(Combo("Projection", projection, modes, 2))
				c.Projection = static_cast<ProjectionType>(projection);
			if(c.Projection == ProjectionType::Perspective)
			{
				Angle("Field of View", c.VerticalFOV, 5.0f, 150.0f);
				Float("Near", c.PerspectiveNear, 0.01f, 0.001f, 100.0f);
				Float("Far", c.PerspectiveFar, 1.0f, 1.0f, 100000.0f);
			}
			else
			{
				Float("Size", c.OrthographicSize, 0.1f, 0.1f, 1000.0f);
				Float("Near", c.OrthographicNear, 0.1f);
				Float("Far", c.OrthographicFar, 0.1f);
			}
			bool primary = c.Primary;
			if(Check("Primary", primary))
			{
				if(primary) // only one primary camera
					ctx.GetScene().Each<CameraComponent>([&](Entity other, CameraComponent& oc) { if(other != entity) oc.Primary = false; });
				c.Primary = primary;
			}
		});

		ComponentHeader<MeshRendererComponent>(entity, "Mesh Renderer", [&](MeshRendererComponent& mr) {
			BeginRow("Mesh");
			ImGui::PushID("mesh");
			float button = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x);
			{
				char meshBuffer[512];
				std::snprintf(meshBuffer, sizeof(meshBuffer), "%s", mr.Mesh.c_str());
				if(ImGui::InputText("##meshpath", meshBuffer, sizeof(meshBuffer)))
					mr.Mesh = meshBuffer;
				Track(false, true);
			}
			ImGui::SameLine();
			if(ImGui::Button("...", ImVec2(button, button)))
				ImGui::OpenPopup("##meshpicker");
			if(ImGui::BeginPopup("##meshpicker"))
			{
				ImGui::SeparatorText("Built-in");
				for(const char* builtin : BuiltinMeshes)
					if(ImGui::Selectable(builtin))
					{
						mr.Mesh = builtin;
						s_EditFinished = true;
					}
				ImGui::SeparatorText("Models");
				static std::vector<AssetPath> models;
				if(ImGui::IsWindowAppearing())
					models = ListAssets({ ".gltf", ".glb" });
				for(const AssetPath& model : models)
					if(ImGui::Selectable(model.c_str()))
					{
						mr.Mesh = model + "#0";
						s_EditFinished = true;
					}
				ImGui::EndPopup();
			}
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
				{
					AssetPath dropped(static_cast<const char*>(payload->Data));
					if(ClassifyAsset(dropped, false) == AssetKind::Model)
					{
						mr.Mesh = dropped + "#0";
						s_EditFinished = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopID();
			AssetField("Material", mr.Material, { ".sfmat" });
			Check("Cast Shadows", mr.CastShadows);
			Check("Receive Shadows", mr.ReceiveShadows);
			Check("Visible", mr.Visible);
		});
		if(auto* mr = entity.TryGetComponent<MeshRendererComponent>())
		{
			if(!mr->Material.empty() && ImGui::TreeNode("Material Properties"))
			{
				DrawMaterialEditor(ctx, mr->Material);
				ImGui::TreePop();
			}
		}

		ComponentHeader<LightComponent>(entity, "Light", [&](LightComponent& l) {
			int type = static_cast<int>(l.Type);
			const char* types[] = { "Directional", "Point", "Spot" };
			Combo("Type", type, types, 3);
			l.Type = static_cast<LightType>(type);
			Color3("Color", l.Color);
			Float("Intensity", l.Intensity, 0.05f, 0.0f, 100000.0f);
			if(l.Type != LightType::Directional)
				Float("Range", l.Range, 0.1f, 0.1f, 10000.0f);
			if(l.Type == LightType::Spot)
			{
				Angle("Inner Cone", l.InnerConeAngle, 0.0f, 85.0f);
				Angle("Outer Cone", l.OuterConeAngle, 1.0f, 89.0f);
				l.InnerConeAngle = std::min(l.InnerConeAngle, l.OuterConeAngle);
			}
			Check("Cast Shadows", l.CastShadows);
			if(l.CastShadows)
			{
				Float("Shadow Softness", l.ShadowSoftness, 0.02f, 0.0f, 8.0f);
				Float("Shadow Bias", l.ShadowBias, 0.002f, 0.0f, 1.0f, "%.4f");
			}
		});

		ComponentHeader<RigidBodyComponent>(entity, "Rigid Body", [&](RigidBodyComponent& rb) {
			int type = static_cast<int>(rb.Type);
			const char* types[] = { "Static", "Kinematic", "Dynamic" };
			Combo("Body Type", type, types, 3);
			rb.Type = static_cast<BodyType>(type);
			if(rb.Type == BodyType::Dynamic)
			{
				Float("Mass", rb.Mass, 0.05f, 0.001f, 100000.0f);
				Float("Linear Damping", rb.LinearDamping, 0.01f, 0.0f, 100.0f);
				Float("Angular Damping", rb.AngularDamping, 0.01f, 0.0f, 100.0f);
				Float("Gravity Factor", rb.GravityFactor, 0.05f, -10.0f, 10.0f);
				Check("Lock Rotation X", rb.LockRotationX);
				Check("Lock Rotation Y", rb.LockRotationY);
				Check("Lock Rotation Z", rb.LockRotationZ);
				Check("Continuous Collision", rb.Continuous);
			}
		});

		ComponentHeader<ColliderComponent>(entity, "Collider", [&](ColliderComponent& c) {
			int shape = static_cast<int>(c.Shape);
			const char* shapes[] = { "Box", "Sphere", "Capsule" };
			Combo("Shape", shape, shapes, 3);
			c.Shape = static_cast<ColliderShape>(shape);
			if(c.Shape == ColliderShape::Box)
				Vec3("Size", c.Size, 1.0f);
			else if(c.Shape == ColliderShape::Sphere)
				Float("Diameter", c.Size.x, 0.05f, 0.01f, 10000.0f);
			else
			{
				Float("Diameter", c.Size.x, 0.05f, 0.01f, 10000.0f);
				Float("Height", c.Size.y, 0.05f, 0.01f, 10000.0f);
			}
			Vec3("Offset", c.Offset, 0.0f);
			Float("Friction", c.Friction, 0.01f, 0.0f, 10.0f);
			Float("Restitution", c.Restitution, 0.01f, 0.0f, 1.0f);
			Check("Is Trigger", c.IsTrigger);
			BeginRow("");
			if(ImGui::Button("Fit To Mesh"))
				if(auto* mr = entity.TryGetComponent<MeshRendererComponent>())
					if(Ref<Mesh> mesh = AssetManager::GetMesh(mr->Mesh))
					{
						glm::vec3 size = mesh->GetBounds().Extents() * 2.0f;
						c.Offset = mesh->GetBounds().Center();
						if(c.Shape == ColliderShape::Box) c.Size = size;
						else if(c.Shape == ColliderShape::Sphere) c.Size = glm::vec3(std::max({ size.x, size.y, size.z }));
						else c.Size = glm::vec3(std::max(size.x, size.z), size.y, 0.0f);
						s_EditFinished = true;
						m_CommitLabel = "Fit Collider";
					}
		});

		ComponentHeader<AudioSourceComponent>(entity, "Audio Source", [&](AudioSourceComponent& a) {
			AssetField("Clip", a.Clip, { ".wav", ".mp3", ".ogg", ".flac" });
			Float("Volume", a.Volume, 0.01f, 0.0f, 4.0f);
			Float("Pitch", a.Pitch, 0.01f, 0.05f, 4.0f);
			Check("Loop", a.Loop);
			Check("Play On Start", a.PlayOnStart);
			Check("Spatial", a.Spatial);
			if(a.Spatial)
			{
				Float("Min Distance", a.MinDistance, 0.1f, 0.0f, 10000.0f);
				Float("Max Distance", a.MaxDistance, 0.5f, 0.1f, 10000.0f);
			}
			if(ctx.IsPlaying() && ctx.GetScene().GetAudioWorld())
			{
				BeginRow("Preview");
				if(ImGui::Button("Play"))
					ctx.GetScene().GetAudioWorld()->Play(entity);
				ImGui::SameLine();
				if(ImGui::Button("Stop"))
					ctx.GetScene().GetAudioWorld()->Stop(entity);
			}
		});

		ComponentHeader<AudioListenerComponent>(entity, "Audio Listener", [&](AudioListenerComponent& l) { Check("Active", l.Active); });

		ComponentHeader<ScriptComponent>(entity, "Script", [&](ScriptComponent& s) {
			AssetField("Script", s.Script, { ".lua" });
			std::string removeKey;
			for(auto& [key, value] : s.Properties)
			{
				ImGui::PushID(key.c_str());
				if(auto* b = std::get_if<bool>(&value)) Check(key.c_str(), *b);
				else if(auto* d = std::get_if<double>(&value))
				{
					float f = static_cast<float>(*d);
					if(Float(key.c_str(), f, 0.05f))
						*d = f;
				}
				else if(auto* str = std::get_if<std::string>(&value)) Text(key.c_str(), *str);
				ImGui::SameLine();
				if(ImGui::SmallButton("x"))
					removeKey = key;
				ImGui::PopID();
			}
			if(!removeKey.empty())
			{
				s.Properties.erase(removeKey);
				s_EditFinished = true;
				m_CommitLabel = "Remove Property";
			}
			BeginRow("New Property");
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputTextWithHint("##pn", "name", m_NewPropertyName, sizeof(m_NewPropertyName));
			ImGui::SameLine();
			ImGui::SetNextItemWidth(70);
			const char* kinds[] = { "number", "bool", "text" };
			ImGui::Combo("##pt", &m_NewPropertyType, kinds, 3);
			ImGui::SameLine();
			if(ImGui::Button("Add") && m_NewPropertyName[0])
			{
				if(m_NewPropertyType == 0) s.Properties[m_NewPropertyName] = 0.0;
				else if(m_NewPropertyType == 1) s.Properties[m_NewPropertyName] = false;
				else s.Properties[m_NewPropertyName] = std::string();
				m_NewPropertyName[0] = 0;
				s_EditFinished = true;
				m_CommitLabel = "Add Property";
			}
			if(ctx.IsPlaying())
			{
				BeginRow("");
				ImGui::TextDisabled("Properties apply when the scene starts");
			}
		});

		ImGui::Spacing();
		if(ImGui::Button("Add Component", ImVec2(-1, 0)))
			ImGui::OpenPopup("AddComponent");
		if(ImGui::BeginPopup("AddComponent"))
		{
			DrawAddComponentMenu(entity);
			ImGui::EndPopup();
		}
	}

	void InspectorPanel::OnImGui(EditorContext& ctx, bool* open)
	{
		if(!ImGui::Begin("Inspector", open))
		{
			ImGui::End();
			return;
		}
		s_EditFinished = false;
		m_CommitLabel.clear();

		if(Entity entity = ctx.GetSelectedEntity())
			DrawEntity(ctx, entity);
		else
			ImGui::TextDisabled("Select an entity to edit its components.");

		if(s_EditFinished)
		{
			ctx.Dirty = true;
			if(!ctx.IsPlaying() && ctx.Commit)
				ctx.Commit(m_CommitLabel.empty() ? "Edit property" : m_CommitLabel);
		}
		ImGui::End();
	}

}
