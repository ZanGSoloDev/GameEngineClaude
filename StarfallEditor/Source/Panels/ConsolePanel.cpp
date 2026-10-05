#include "ConsolePanel.h"

#include "../AssetFiles.h"
#include "Starfall/Scripting/ScriptWorld.h"

#include <algorithm>

namespace StarfallEditor {

	namespace {

		ImVec4 LevelColor(LogLevel level)
		{
			switch(level)
			{
				case LogLevel::Trace: return { 0.6f, 0.6f, 0.65f, 1 };
				case LogLevel::Info: return { 0.85f, 0.88f, 0.92f, 1 };
				case LogLevel::Warn: return { 1.0f, 0.8f, 0.3f, 1 };
				case LogLevel::Error: return { 1.0f, 0.4f, 0.4f, 1 };
				case LogLevel::Critical: return { 1.0f, 0.2f, 0.5f, 1 };
			}
			return { 1, 1, 1, 1 };
		}

		const char* LevelTag(LogLevel level)
		{
			switch(level)
			{
				case LogLevel::Trace: return "TRACE";
				case LogLevel::Info: return "INFO ";
				case LogLevel::Warn: return "WARN ";
				case LogLevel::Error: return "ERROR";
				case LogLevel::Critical: return "CRIT ";
			}
			return "";
		}

	}

	void ConsolePanel::OnImGui(EditorContext& ctx, bool* open)
	{
		if(!ImGui::Begin("Console", open))
		{
			ImGui::End();
			return;
		}
		if(ImGui::Button("Clear"))
			ctx.Console.clear();
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &m_ShowTrace);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_ShowInfo);
		ImGui::SameLine();
		ImGui::Checkbox("Warn", &m_ShowWarn);
		ImGui::SameLine();
		ImGui::Checkbox("Error", &m_ShowError);
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##filter", "Filter...", m_Filter, sizeof(m_Filter));
		ImGui::Separator();

		ImGui::BeginChild("##log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		for(const ConsoleLine& line : ctx.Console)
		{
			bool show = (line.Level == LogLevel::Trace && m_ShowTrace) || (line.Level == LogLevel::Info && m_ShowInfo) ||
				(line.Level == LogLevel::Warn && m_ShowWarn) || (line.Level >= LogLevel::Error && m_ShowError);
			if(!show)
				continue;
			if(m_Filter[0] && line.Message.find(m_Filter) == std::string::npos)
				continue;
			ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(line.Level));
			ImGui::Text("[%s] %s: %s", LevelTag(line.Level), line.Channel.c_str(), line.Message.c_str());
			ImGui::PopStyleColor();
		}
		if(m_AutoScroll && ctx.ConsoleDirty)
			ImGui::SetScrollHereY(1.0f);
		ctx.ConsoleDirty = false;
		ImGui::EndChild();

		ImGui::End();
	}

	void SettingsPanel::OnImGui(EditorContext& ctx, bool* open)
	{
		if(!ImGui::Begin("Scene Settings", open))
		{
			ImGui::End();
			return;
		}
		SceneSettings& s = ctx.GetScene().GetSettings();
		bool changed = false;

		if(ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// HDRI picker
			char hdri[256];
			std::snprintf(hdri, sizeof(hdri), "%s", s.Environment.HDRI.c_str());
			ImGui::SetNextItemWidth(-60);
			if(ImGui::InputTextWithHint("##hdri", "HDRI (procedural sky if empty)", hdri, sizeof(hdri)))
			{
				s.Environment.HDRI = hdri;
				changed = true;
			}
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET"))
				{
					AssetPath dropped(static_cast<const char*>(payload->Data));
					if(ClassifyAsset(dropped, false) == AssetKind::Hdri)
					{
						s.Environment.HDRI = dropped;
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if(ImGui::Button("Pick"))
				ImGui::OpenPopup("##hdripicker");
			if(ImGui::BeginPopup("##hdripicker"))
			{
				static std::vector<AssetPath> hdris;
				if(ImGui::IsWindowAppearing())
					hdris = ListAssets({ ".hdr" });
				if(ImGui::Selectable("<Procedural sky>"))
				{
					s.Environment.HDRI.clear();
					changed = true;
				}
				for(const AssetPath& h : hdris)
					if(ImGui::Selectable(h.c_str()))
					{
						s.Environment.HDRI = h;
						changed = true;
					}
				ImGui::EndPopup();
			}
			changed |= ImGui::DragFloat("Intensity", &s.Environment.Intensity, 0.01f, 0.0f, 20.0f);
			changed |= ImGui::DragFloat("Rotation", &s.Environment.RotationDegrees, 0.5f, -360.0f, 360.0f, "%.1f deg");
			changed |= ImGui::Checkbox("Show Skybox", &s.Environment.ShowSkybox);
			if(s.Environment.HDRI.empty())
			{
				changed |= ImGui::ColorEdit3("Sky Color", &s.Environment.SkyColor.x);
				changed |= ImGui::ColorEdit3("Ground Color", &s.Environment.GroundColor.x);
			}
		}

		if(ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			changed |= ImGui::DragFloat("Exposure", &s.PostProcess.Exposure, 0.01f, 0.01f, 20.0f);
			int mode = static_cast<int>(s.PostProcess.TonemapMode);
			const char* modes[] = { "None", "Reinhard", "ACES", "AgX" };
			if(ImGui::Combo("Tonemapper", &mode, modes, 4))
			{
				s.PostProcess.TonemapMode = static_cast<Tonemapper>(mode);
				changed = true;
			}
			changed |= ImGui::Checkbox("SSAO", &s.PostProcess.SSAOEnabled);
			if(s.PostProcess.SSAOEnabled)
			{
				changed |= ImGui::DragFloat("SSAO Radius", &s.PostProcess.SSAORadius, 0.01f, 0.05f, 5.0f);
				changed |= ImGui::DragFloat("SSAO Intensity", &s.PostProcess.SSAOIntensity, 0.01f, 0.0f, 4.0f);
				changed |= ImGui::SliderInt("SSAO Samples", &s.PostProcess.SSAOSamples, 6, 48);
			}
		}

		if(ImGui::CollapsingHeader("Shadows"))
		{
			int size = static_cast<int>(s.Shadows.MapSize);
			const char* sizes[] = { "512", "1024", "2048", "4096" };
			int index = size <= 512 ? 0 : size <= 1024 ? 1 : size <= 2048 ? 2 : 3;
			if(ImGui::Combo("Map Size", &index, sizes, 4))
			{
				s.Shadows.MapSize = 512u << index;
				changed = true;
			}
			int cascades = static_cast<int>(s.Shadows.CascadeCount);
			if(ImGui::SliderInt("Cascades", &cascades, 1, 4))
			{
				s.Shadows.CascadeCount = static_cast<uint32_t>(cascades);
				changed = true;
			}
			changed |= ImGui::DragFloat("Max Distance", &s.Shadows.MaxDistance, 0.5f, 5.0f, 1000.0f);
			changed |= ImGui::SliderFloat("Split Lambda", &s.Shadows.CascadeSplitLambda, 0.0f, 1.0f);
		}

		if(ImGui::CollapsingHeader("Physics"))
			changed |= ImGui::DragFloat3("Gravity", &s.Gravity.x, 0.05f);

		if(changed)
		{
			ctx.Dirty = true;
			if(!ctx.IsPlaying() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ctx.Commit)
				ctx.Commit("Scene settings");
		}
		if(!ctx.IsPlaying() && ImGui::IsItemDeactivatedAfterEdit() && ctx.Commit)
			ctx.Commit("Scene settings");

		ImGui::End();

		if(ImGui::Begin("Stats"))
		{
			const RenderStats& st = ctx.Stats;
			ImGui::Text("GPU: %s", ctx.AdapterName.c_str());
			ImGui::Text("Frame: %.2f ms (%.0f FPS)", ctx.FrameMilliseconds, ctx.FrameMilliseconds > 0.0f ? 1000.0f / ctx.FrameMilliseconds : 0.0f);
			ImGui::Separator();
			ImGui::Text("Entities: %zu", ctx.GetScene().GetEntityCount());
			ImGui::Text("Draw calls: %u (shadow %u)", st.DrawCalls, st.ShadowDrawCalls);
			ImGui::Text("Triangles: %u", st.Triangles);
			ImGui::Text("Meshes: %u visible, %u culled", st.MeshesSubmitted, st.MeshesCulled);
			ImGui::Text("Local lights: %u (%u shadowed)", st.LocalLights, st.ShadowedLocalLights);
			ImGui::Text("Shadow cascades: %u", st.Cascades);
			ImGui::Separator();
			ImGui::Checkbox("Grid", &ctx.ShowGrid);
			ImGui::Checkbox("Colliders", &ctx.ShowColliders);
			ImGui::Checkbox("Light/Camera gizmos", &ctx.ShowLightGizmos);
			ImGui::Checkbox("Shadows", &ctx.EnableShadows);
			ImGui::Checkbox("SSAO", &ctx.EnableSSAO);
			ImGui::Checkbox("VSync", &ctx.VSync);
		}
		ImGui::End();
	}

}
