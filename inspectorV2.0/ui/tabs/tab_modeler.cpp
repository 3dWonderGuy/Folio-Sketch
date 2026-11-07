/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_modeler.cpp
 * @brief Workspace 1: Centerline ODE Physics, Wobble Filter & Prediction Controls
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace Folio::V2 {

void RenderTabModeler(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("1. Modeler (Centerline & Filters)")) {
        if (gui.currentWorkspaceTab != 0) {
            gui.currentWorkspaceTab = 0;
            needsRedraw = true;
        }

        auto& inkEngine = layerManager.GetLiveEngine();
        auto config = inkEngine.GetConfig();
        bool configDirty = false;

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Google Ink Stroke Modeler ODE & Centerline Physics");

        if (ImGui::CollapsingHeader("Spring-Mass-Damper ODE & Rate", ImGuiTreeNodeFlags_DefaultOpen)) {
            configDirty |= ImGui::SliderFloat("spring_constant (k/m)", &config.physics.springMassConstant,
                                              0.00005f, 0.00500f, "%.5f");
            configDirty |= ImGui::SliderFloat("drag_coefficient (gamma)", &config.physics.dragConstant,
                                              10.0f, 200.0f, "%.1f");
            configDirty |= ImGui::SliderFloat("min_output_rate (Hz)", &config.physics.minOutputRateHz,
                                              60.0f, 480.0f, "%.0f Hz");
            configDirty |= ImGui::SliderFloat("endOfStrokeStoppingDist (mm)", &config.physics.endOfStrokeStoppingDistanceMm,
                                              0.001f, 0.100f, "%.4f mm");
            configDirty |= ImGui::SliderInt("endOfStrokeMaxIterations", &config.physics.endOfStrokeMaxIterations,
                                            1, 50);
        }

        if (ImGui::CollapsingHeader("Wobble / Tremor Filter", ImGuiTreeNodeFlags_DefaultOpen)) {
            configDirty |= ImGui::Checkbox("Enable Wobble Filter", &config.filters.enableWobble);
            if (config.filters.enableWobble) {
                configDirty |= ImGui::SliderFloat("Wobble timeout (s)", &config.filters.wobbleTimeoutSec,
                                                  0.005f, 0.500f, "%.3f s");
                configDirty |= ImGui::SliderFloat("Wobble speed_floor", &config.filters.wobbleSpeedFloor,
                                                  0.0f, 100.0f, "%.2f mm/s");
                configDirty |= ImGui::SliderFloat("Wobble speed_ceiling", &config.filters.wobbleSpeedCeiling,
                                                  0.1f, 200.0f, "%.2f mm/s");
                if (config.filters.wobbleSpeedCeiling < config.filters.wobbleSpeedFloor) {
                    config.filters.wobbleSpeedCeiling = config.filters.wobbleSpeedFloor + 0.1f;
                }
                ImGui::TextDisabled("Smoothing = 100%% at <= floor, tapering smoothly to 0%% at >= ceiling.");
            }
        }

        if (ImGui::CollapsingHeader("Loop Contraction Mitigation & Normal Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Loop Contraction Mitigation", &config.filters.enableLoopMitigation)) {
                if (config.filters.enableLoopMitigation) {
                    config.physics.useStrokeNormalProjection = true;
                }
                configDirty = true;
            }
            if (config.filters.enableLoopMitigation) {
                configDirty |= ImGui::SliderFloat("Loop speed_lower (px/s)", &config.filters.loopSpeedLowerBound,
                                                  0.0f, 2000.0f, "%.0f px/s");
                configDirty |= ImGui::SliderFloat("Loop speed_upper (px/s)", &config.filters.loopSpeedUpperBound,
                                                  100.0f, 5000.0f, "%.0f px/s");
                configDirty |= ImGui::SliderFloat("Strength at lower speed", &config.filters.loopStrengthAtLowerSpeed,
                                                  0.0f, 1.0f, "%.2f (1.0=standard spring)");
                configDirty |= ImGui::SliderFloat("Strength at upper speed", &config.filters.loopStrengthAtUpperSpeed,
                                                  0.0f, 1.0f, "%.2f (0=raw, 1=spring)");
                configDirty |= ImGui::SliderFloat("Loop min speed window", &config.filters.loopMinSpeedWindowSec,
                                                  0.005f, 0.200f, "%.3f s");
                ImGui::TextDisabled("Lower strength pulls high-speed cursive loops closer to raw points.");
            }
            if (ImGui::Checkbox("use_stroke_normal_projection", &config.physics.useStrokeNormalProjection)) {
                if (!config.physics.useStrokeNormalProjection && config.filters.enableLoopMitigation) {
                    config.filters.enableLoopMitigation = false;
                }
                configDirty = true;
            }
        }

        if (ImGui::CollapsingHeader("Kalman Prediction Engine (Ahead of Pen)", ImGuiTreeNodeFlags_DefaultOpen)) {
            configDirty |= ImGui::Checkbox("Enable Kalman Prediction", &config.prediction.enable);
            if (config.prediction.enable) {
                configDirty |= ImGui::SliderFloat("Prediction Duration (ms)", &config.prediction.durationMs,
                                                  0.0f, 50.0f, "%.1f ms");
                configDirty |= ImGui::SliderFloat("Acceleration Weight", &config.prediction.accelerationWeight,
                                                  0.0f, 1.0f, "%.2f");
                configDirty |= ImGui::SliderFloat("Jerk Weight", &config.prediction.jerkWeight,
                                                  0.0f, 1.0f, "%.2f");
                configDirty |= ImGui::SliderFloat("Process Noise", &config.prediction.processNoise,
                                                  0.001f, 1.0f, "%.3f");
                configDirty |= ImGui::SliderFloat("Measurement Noise", &config.prediction.measurementNoise,
                                                  0.01f, 2.0f, "%.3f");
            }
        }

        if (ImGui::CollapsingHeader("Physics Angular Upsampling / Curvature Rate", ImGuiTreeNodeFlags_DefaultOpen)) {
            configDirty |= ImGui::Checkbox("Enable Angular Curve Upsampling", &config.physics.enableAngleUpsampling);
            if (config.physics.enableAngleUpsampling) {
                float angleDeg = config.physics.maxEstimatedAngleRad * 57.2957795f;
                if (ImGui::SliderFloat("Max Angle per Sample", &angleDeg, 2.0f, 30.0f, "%.1f°")) {
                    config.physics.maxEstimatedAngleRad = angleDeg / 57.2957795f;
                    configDirty = true;
                }
            }
        }

        ImGui::SeparatorText("Live Diagnostics Overlays");
        ImGui::Checkbox("Raw Connected Polyline (Gray 1px Lines)", &gui.showRawPolyline);
        ImGui::Checkbox("Raw Hardware Crosshairs (+)", &gui.showRawCrosshairs);
        ImGui::Checkbox("Raw Input Dots (Red Circles)", &gui.showRawPoints);
        ImGui::Checkbox("Modeled Centerline (Yellow Spline)", &gui.showModeledCenterline);
        ImGui::Checkbox("Kalman Prediction Spine (Cyan Trail)", &gui.showKalmanPrediction);
        ImGui::Separator();
        if (ImGui::Checkbox("Show Solid Ink Mesh in Modeler (Override)", &gui.showSolidMeshInModeler)) {
            needsRedraw = true;
        }

        ImGui::SeparatorText("Captured Stroke Telemetry & Benchmark Tests");
        const auto& rawStroke = layerManager.GetLastRawSamples();
        ImGui::Text("Captured Raw Samples: %zu", rawStroke.size());
        if (ImGui::Button("Dump Raw Stroke to Console / Clipboard")) {
            if (!rawStroke.empty()) {
                std::string dumpStr;
                for (const auto& s : rawStroke) {
                    char lbuf[128];
                    std::snprintf(lbuf, sizeof(lbuf), "    { %.6f, %.6f, %.4ff, %.4ff, %.4ff, 0.0f },\n",
                                  s.x, s.y, 12.5f, s.pressure, s.tiltRad);
                    dumpStr += lbuf;
                }
                ImGui::SetClipboardText(dumpStr.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-Run Modeler on Captured Samples")) {
            layerManager.ReplayRawStroke();
            needsRedraw = true;
        }

        if (ImGui::Button("Inject Test Scribble")) {
            layerManager.InjectTestScribble();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Load V1 Repro114 Defect Test")) {
            layerManager.LoadRepro114();
            needsRedraw = true;
        }

        if (configDirty) {
            inkEngine.SetConfig(config);
            if (!layerManager.IsLiveInkingActive()) {
                layerManager.ReplayRawStroke();
            }
            needsRedraw = true;
        }

        ImGui::EndTabItem();
    }
}

} // namespace Folio::V2
