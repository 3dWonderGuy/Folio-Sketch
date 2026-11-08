/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_dynamics.cpp
 * @brief Workspace 2: Width Evaluator, Velocity Dynamics & Pressure Calibration
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Folio::V2 {

void RenderTabDynamics(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("2. Width & Dynamics")) {
        if (gui.currentWorkspaceTab != 1) {
            gui.currentWorkspaceTab = 1;
            needsRedraw = true;
        }

        auto& liveEngine = layerManager.GetLiveEngine();
        auto config = liveEngine.GetConfig();
        auto& style = layerManager.GetActiveStyle();
        bool configDirty = false;
        bool styleDirty = false;

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Dynamic Radius (r_i) Evaluation & Width Curve");

        ImGui::SeparatorText("Dynamic Inputs");
        if (ImGui::Checkbox("usePressure", &config.dynamics.usePressure)) {
            style.usePressure = config.dynamics.usePressure;
            configDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("useVelocity", &config.dynamics.useVelocity)) {
            style.useVelocity = config.dynamics.useVelocity;
            configDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("useTilt", &config.dynamics.useTilt)) {
            style.useTilt = config.dynamics.useTilt;
            configDirty = true;
        }

        if (config.dynamics.useTilt) {
            configDirty |= ImGui::SliderFloat("maxTiltWidthMultiplier", &config.dynamics.maxTiltWidthMultiplier, 1.0f, 5.0f, "%.2f");
            float tiltDeadDeg = config.dynamics.tiltDeadzoneRad * 57.2957795f;
            if (ImGui::SliderFloat("tiltDeadzone (deg)", &tiltDeadDeg, 0.0f, 45.0f, "%.1f°")) {
                config.dynamics.tiltDeadzoneRad = tiltDeadDeg / 57.2957795f;
                configDirty = true;
            }
            float tiltMaxDeg = config.dynamics.tiltMaxAngleRad * 57.2957795f;
            if (ImGui::SliderFloat("tiltMaxAngle (deg)", &tiltMaxDeg, 30.0f, 85.0f, "%.1f°")) {
                config.dynamics.tiltMaxAngleRad = tiltMaxDeg / 57.2957795f;
                configDirty = true;
            }
        }

        ImGui::SeparatorText("Thickness & Ratio Bounds");
        styleDirty  |= ImGui::SliderFloat("baseWidth (px)", &style.baseWidth, 0.5f, 50.0f, "%.2f px");
        configDirty |= ImGui::SliderFloat("minWidthRatio", &config.dynamics.minWidthMultiplier, 0.05f, 1.0f, "%.2f");
        configDirty |= ImGui::SliderFloat("maxWidthRatio", &config.dynamics.maxWidthMultiplier, 0.5f, 4.0f, "%.2f");
        configDirty |= ImGui::SliderFloat("velocityMaxClamp", &config.dynamics.velocityThinningMaxSpeed, 50.0f, 3500.0f, "%.0f px/s");
        configDirty |= ImGui::SliderFloat("Width EMA Smoothing Factor", &config.dynamics.widthSmoothingEma, 0.01f, 1.0f, "%.2f");
        configDirty |= ImGui::SliderFloat("Velocity EMA Smoothing Factor", &config.dynamics.velocitySmoothingEma, 0.01f, 1.0f, "%.2f");

        ImGui::SeparatorText("Digitizer Pressure Curve");
        configDirty |= ImGui::Checkbox("enablePressureCurve", &config.calibration.enablePressureCurve);
        if (config.calibration.enablePressureCurve) {
            configDirty |= ImGui::SliderFloat("Pressure Gamma", &config.calibration.pressureGamma, 0.25f, 3.0f, "%.2f");
            configDirty |= ImGui::SliderFloat("Pressure Deadzone Min", &config.calibration.pressureDeadzoneMin, 0.0f, 0.25f, "%.3f");
            configDirty |= ImGui::SliderFloat("Pressure Saturation Max", &config.calibration.pressureSaturationMax, 0.50f, 1.0f, "%.3f");
        }

        ImGui::SeparatorText("Acceleration & Corner Compensation");
        if (ImGui::Button("Reset Dynamics to Recommended Defaults")) {
            config.dynamics.useAccelerationFiltering = true;
            config.dynamics.maxAccelerationMmPerSec2 = 3500.0f;
            config.dynamics.maxDecelerationMmPerSec2 = 2500.0f;
            config.dynamics.useCornerCompensation = true;
            config.dynamics.cornerCompensationThresholdRad = 0.52359877f;
            config.dynamics.enableSpatialSlopeClamping = true;
            config.dynamics.maxRadiusSlopePerMm = 0.35f;
            configDirty = true;
        }
        configDirty |= ImGui::Checkbox("useAccelerationFiltering", &config.dynamics.useAccelerationFiltering);
        ImGui::SameLine();
        configDirty |= ImGui::Checkbox("useCornerCompensation", &config.dynamics.useCornerCompensation);
        configDirty |= ImGui::SliderFloat("maxAcceleration", &config.dynamics.maxAccelerationMmPerSec2, 500.0f, 10000.0f, "%.0f");
        configDirty |= ImGui::SliderFloat("maxDeceleration", &config.dynamics.maxDecelerationMmPerSec2, 500.0f, 10000.0f, "%.0f");
        float cornerDeg = config.dynamics.cornerCompensationThresholdRad * 57.2957795f;
        if (ImGui::SliderFloat("cornerThreshold (deg)", &cornerDeg, 5.0f, 90.0f, "%.1f°")) {
            config.dynamics.cornerCompensationThresholdRad = cornerDeg / 57.2957795f;
            configDirty = true;
        }
        configDirty |= ImGui::Checkbox("enableSpatialSlopeClamping", &config.dynamics.enableSpatialSlopeClamping);
        if (config.dynamics.enableSpatialSlopeClamping) {
            configDirty |= ImGui::SliderFloat("maxRadiusSlope (dr/ds)", &config.dynamics.maxRadiusSlopePerMm, 0.05f, 1.0f, "%.2f");
        }

        ImGui::SeparatorText("Gaussian Peak Smoothing & Tail Taper");
        configDirty |= ImGui::Checkbox("enableGaussianRadiusSmoothing", &config.dynamics.enableGaussianRadiusSmoothing);
        if (config.dynamics.enableGaussianRadiusSmoothing) {
            configDirty |= ImGui::SliderFloat("Gaussian Sigma", &config.dynamics.radiusSmoothingKernelSigmaMm, 0.5f, 15.0f, "%.1f");
            configDirty |= ImGui::SliderInt("Smoothing Passes", &config.dynamics.radiusSmoothingPasses, 1, 5);
        }
        configDirty |= ImGui::SliderFloat("tailTaperLength (mm)", &config.dynamics.tailTaperLengthMm, 0.0f, 20.0f, "%.1f mm");
        if (config.dynamics.tailTaperLengthMm > 0.0f) {
            configDirty |= ImGui::SliderFloat("tailTaperPower", &config.dynamics.tailTaperPower, 0.5f, 3.0f, "%.2f");
        }

        ImGui::SeparatorText("Radius Overlays & Width Curve Plot");
        ImGui::Checkbox("Show Hollow Radius Discs (r_i)", &gui.showRadiusDiscs);

        // Compute arc-length parameterized radius profile for PlotLines
        const auto& ptsForPlot = layerManager.IsLiveInkingActive() ? liveEngine.GetLiveModeledPoints() : layerManager.GetLastFinishedPoints();

        gui.arcLengthPlotSamples.clear();
        float totalArcLen = 0.0f;
        float minR = 0.0f, maxR = 1.0f;

        if (!ptsForPlot.empty()) {
            minR = ptsForPlot[0].r;
            maxR = ptsForPlot[0].r;
            for (size_t i = 1; i < ptsForPlot.size(); ++i) {
                float ds = static_cast<float>(std::hypot(ptsForPlot[i].x - ptsForPlot[i - 1].x,
                                                         ptsForPlot[i].y - ptsForPlot[i - 1].y));
                totalArcLen += ds;
                minR = std::min(minR, ptsForPlot[i].r);
                maxR = std::max(maxR, ptsForPlot[i].r);
            }

            constexpr int kSampleCount = 120;
            gui.arcLengthPlotSamples.resize(kSampleCount);
            for (int b = 0; b < kSampleCount; ++b) {
                float targetS = (static_cast<float>(b) / static_cast<float>(kSampleCount - 1)) * totalArcLen;
                float accum = 0.0f;
                float foundR = ptsForPlot.back().r;
                for (size_t i = 1; i < ptsForPlot.size(); ++i) {
                    float ds = static_cast<float>(std::hypot(ptsForPlot[i].x - ptsForPlot[i - 1].x,
                                                             ptsForPlot[i].y - ptsForPlot[i - 1].y));
                    if (accum + ds >= targetS || i + 1 == ptsForPlot.size()) {
                        float t = (ds > 1e-4f) ? std::clamp((targetS - accum) / ds, 0.0f, 1.0f) : 0.0f;
                        foundR = ptsForPlot[i - 1].r + t * (ptsForPlot[i].r - ptsForPlot[i - 1].r);
                        break;
                    }
                    accum += ds;
                }
                gui.arcLengthPlotSamples[b] = foundR;
            }
        }

        if (!gui.arcLengthPlotSamples.empty()) {
            char overlayBuf[96];
            std::snprintf(overlayBuf, sizeof(overlayBuf),
                          "r in [%.2f, %.2f] | ArcLen: %.1f px", minR, maxR, totalArcLen);
            float plotMax = std::max(maxR * 1.15f, 1.0f);
            ImGui::PlotLines("Radius vs Arc-Length",
                             gui.arcLengthPlotSamples.data(),
                             static_cast<int>(gui.arcLengthPlotSamples.size()),
                             0,
                             overlayBuf,
                             0.0f,
                             plotMax,
                             ImVec2(0.0f, 90.0f));
        } else {
            const float zeroVal = 0.0f;
            ImGui::PlotLines("Radius vs Arc-Length", &zeroVal, 1, 0, "No stroke data", 0.0f, 1.0f, ImVec2(0.0f, 90.0f));
        }

        if (configDirty || styleDirty) {
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
