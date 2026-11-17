/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_postprocessing.cpp
 * @brief Workspace 3: Ramer-Douglas-Peucker (RDP) & Decimation Telemetry
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include <imgui.h>

namespace Folio::V2 {

void RenderTabPostProcessing(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("3. Post-Processing & Simplification")) {
        if (gui.currentWorkspaceTab != 2) {
            gui.currentWorkspaceTab = 2;
            needsRedraw = true;
        }

        auto& liveEngine = layerManager.GetLiveEngine();
        auto config = liveEngine.GetConfig();
        bool configDirty = false;

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Ramer-Douglas-Peucker (RDP) & Stationary Envelopment");

        // 1. Live Decimation Telemetry Card
        ImGui::SeparatorText("Live Decimation Telemetry");
        const auto& rawModeled = layerManager.GetLastRawModeledPoints();
        const auto& finished = layerManager.GetLastFinishedPoints();

        size_t preCount = rawModeled.size();
        size_t postCount = finished.size();

        if (layerManager.IsLiveInkingActive()) {
            ImGui::TextDisabled("Stroke in progress... Telemetry updates upon pen release.");
        } else if (preCount > 0) {
            float reduction = 0.0f;
            if (preCount > postCount) {
                reduction = (1.0f - static_cast<float>(postCount) / static_cast<float>(preCount)) * 100.0f;
            }
            float compRatio = (postCount > 0) ? (static_cast<float>(preCount) / static_cast<float>(postCount)) : 1.0f;

            ImGui::Text("Modeled Points (Pre-RDP):     %zu", preCount);
            ImGui::Text("Retained Vertices (Post-RDP):  %zu", postCount);
            ImGui::Text("Decimation Reduction:          %.1f%%  (%.1fx Compression)", reduction, compRatio);

            ImGui::ProgressBar(reduction / 100.0f, ImVec2(-1.0f, 18.0f));
        } else {
            ImGui::TextDisabled("No stroke captured yet. Draw a stroke or inject a scribble below.");
        }

        // 2. Simplification Presets
        ImGui::SeparatorText("Quick Simplification Presets");
        if (ImGui::Button("Ultra High-Fidelity")) {
            config.postProcessing.enableSimplification = true;
            config.postProcessing.simplificationToleranceMm = 0.005f;
            config.postProcessing.simplificationMaxTurnDeg = 2.0f;
            config.postProcessing.simplificationRadiusTolerance = 0.04f;
            configDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced (Default)")) {
            config.postProcessing.enableSimplification = true;
            config.postProcessing.simplificationToleranceMm = 0.015f;
            config.postProcessing.simplificationMaxTurnDeg = 4.0f;
            config.postProcessing.simplificationRadiusTolerance = 0.08f;
            configDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Aggressive Reduction")) {
            config.postProcessing.enableSimplification = true;
            config.postProcessing.simplificationToleranceMm = 0.060f;
            config.postProcessing.simplificationMaxTurnDeg = 12.0f;
            config.postProcessing.simplificationRadiusTolerance = 0.20f;
            configDirty = true;
        }

        // 3. Post-Processing Algorithm Tuning
        ImGui::SeparatorText("Algorithm Parameters");
        configDirty |= ImGui::Checkbox("Stationary Envelopment (Prune Redundant Interior)",
                                       &config.postProcessing.stationaryEnvelopment);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prunes redundant interior micro-clusters when pen dwells in place without lift.");
        }

        configDirty |= ImGui::Checkbox("Enable RDP Simplification",
                                       &config.postProcessing.enableSimplification);

        if (config.postProcessing.enableSimplification) {
            configDirty |= ImGui::SliderFloat("Simplification Tolerance (mm)",
                                              &config.postProcessing.simplificationToleranceMm,
                                              0.001f, 0.150f, "%.4f mm");
            configDirty |= ImGui::SliderFloat("Max Turn Angle (deg)",
                                              &config.postProcessing.simplificationMaxTurnDeg,
                                              0.5f, 30.0f, "%.1f°");
            configDirty |= ImGui::SliderFloat("Radius Change Tolerance",
                                              &config.postProcessing.simplificationRadiusTolerance,
                                              0.01f, 0.50f, "%.2f");
            configDirty |= ImGui::SliderFloat("Min Joint Cap Angle (deg)",
                                              &config.postProcessing.minTurnAngleForJointCapDeg,
                                              0.5f, 20.0f, "%.1f°");
        }

        // 4. Visual Diagnostics Overlays
        ImGui::SeparatorText("Visual Decimation Overlays");
        ImGui::Checkbox("Show Pre-RDP Modeled Points (Amber Line & Dots)", &gui.showRdpOriginalPoints);
        ImGui::Checkbox("Show Retained Key Vertices (Bright Green Nodes)", &gui.showRdpKeyVertices);

        // 5. Test Actions
        ImGui::SeparatorText("Actions & Tests");
        if (ImGui::Button("Re-Run Simplification on Last Stroke")) {
            layerManager.ReplayRawStroke();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Inject Test Scribble")) {
            layerManager.InjectTestScribble();
            needsRedraw = true;
        }

        if (configDirty) {
            liveEngine.SetConfig(config);
            if (!layerManager.IsLiveInkingActive()) {
                layerManager.ReplayRawStroke();
            }
            needsRedraw = true;
        }

        ImGui::EndTabItem();
    }
}

} // namespace Folio::V2
