#include "debug/modeler_inspector.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio::Debug {

bool ModelerInspector::RenderUI(InkingConfig& config) {
    bool physicsDirty = false;

    ImGui::SeparatorText("Position Modeler (Spring-Mass-Damper)");
    physicsDirty |= ImGui::SliderFloat("spring_constant", &config.physics.springMassConstant,
                                       0.00005f, 0.00500f, "%.5f");
    physicsDirty |= ImGui::SliderFloat("drag_coefficient", &config.physics.dragConstant,
                                       10.0f, 200.0f, "%.1f");

    ImGui::SeparatorText("Sampling & End-of-Stroke");
    physicsDirty |= ImGui::SliderFloat("min_output_rate (Hz)", &config.physics.minOutputRateHz,
                                       30.0f, 720.0f, "%.0f Hz");
    physicsDirty |= ImGui::SliderFloat("end_of_stroke_stopping_distance", &config.physics.endOfStrokeStoppingDistanceMm,
                                       0.0001f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
    physicsDirty |= ImGui::SliderInt("end_of_stroke_max_iterations", &config.physics.endOfStrokeMaxIterations,
                                     1, 200);

    ImGui::SeparatorText("Wobble Smoother (Tremor Filter)");
    physicsDirty |= ImGui::Checkbox("Wobble is_enabled", &config.filters.enableWobble);
    if (config.filters.enableWobble) {
        physicsDirty |= ImGui::SliderFloat("Wobble timeout (s)", &config.filters.wobbleTimeoutSec,
                                           0.005f, 0.500f, "%.3f s");
        physicsDirty |= ImGui::SliderFloat("Wobble speed_floor", &config.filters.wobbleSpeedFloor,
                                           0.0f, 100.0f, "%.2f");
        physicsDirty |= ImGui::SliderFloat("Wobble speed_ceiling", &config.filters.wobbleSpeedCeiling,
                                           0.1f, 200.0f, "%.2f");
        if (config.filters.wobbleSpeedCeiling < config.filters.wobbleSpeedFloor) {
            config.filters.wobbleSpeedCeiling = config.filters.wobbleSpeedFloor + 0.1f;
        }
        ImGui::TextDisabled("Smoothing factor = 100%% at <= floor, tapering to 0%% at >= ceiling.");
    }

    ImGui::SeparatorText("Loop Contraction & Projection");
    if (ImGui::Checkbox("Loop Contraction Mitigation is_enabled", &config.filters.enableLoopMitigation)) {
        if (config.filters.enableLoopMitigation) {
            // Google StrokeModeler validation requires normal projection when loop mitigation is enabled
            config.physics.useStrokeNormalProjection = true;
        }
        physicsDirty = true;
    }
    if (config.filters.enableLoopMitigation) {
        physicsDirty |= ImGui::SliderFloat("Loop speed_lower (px/s)", &config.filters.loopSpeedLowerBound,
                                           0.0f, 2000.0f, "%.0f px/s");
        physicsDirty |= ImGui::SliderFloat("Loop speed_upper (px/s)", &config.filters.loopSpeedUpperBound,
                                           100.0f, 5000.0f, "%.0f px/s");
        physicsDirty |= ImGui::SliderFloat("Strength at low speed", &config.filters.loopStrengthAtLowerSpeed,
                                           0.0f, 1.0f, "%.2f (1=spring)");
        physicsDirty |= ImGui::SliderFloat("Strength at high speed", &config.filters.loopStrengthAtUpperSpeed,
                                           0.0f, 1.0f, "%.2f (0=raw, 1=spring)");
        physicsDirty |= ImGui::SliderFloat("Min speed sampling window (s)", &config.filters.loopMinSpeedWindowSec,
                                           0.005f, 0.200f, "%.3f s");
        ImGui::TextDisabled("Lower value pulls high-speed corners closer to raw points.");
    }
    if (ImGui::Checkbox("use_stroke_normal_projection", &config.physics.useStrokeNormalProjection)) {
        if (!config.physics.useStrokeNormalProjection && config.filters.enableLoopMitigation) {
            config.filters.enableLoopMitigation = false;
        }
        physicsDirty = true;
    }

    ImGui::SeparatorText("Kalman Prediction");
    physicsDirty |= ImGui::Checkbox("google_enable_prediction", &config.prediction.enable);
    if (config.prediction.enable) {
        auto& pred = config.prediction;
        if (ImGui::Button("Reset Prediction to Stable Defaults")) {
            pred = InkingConfig::Prediction{};
            physicsDirty = true;
        }
        physicsDirty |= ImGui::SliderFloat("Prediction Duration (ms)", &pred.durationMs,
                                           0.0f, 50.0f, "%.1f ms");

        ImGui::TextDisabled("Kalman filter");
        physicsDirty |= ImGui::SliderFloat("acceleration_weight", &pred.accelerationWeight,
                                           0.0f, 1.0f, "%.2f (0 = constant velocity)");
        physicsDirty |= ImGui::SliderFloat("jerk_weight", &pred.jerkWeight,
                                           0.0f, 1.0f, "%.2f");
        physicsDirty |= ImGui::SliderFloat("process_noise", &pred.processNoise,
                                           0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        physicsDirty |= ImGui::SliderFloat("measurement_noise", &pred.measurementNoise,
                                           0.01f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        physicsDirty |= ImGui::SliderInt("min_stable_iteration", &pred.minStableIteration, 1, 20);
        physicsDirty |= ImGui::SliderFloat("min_catchup_velocity", &pred.minCatchupVelocity,
                                           0.0f, 1.0f, "%.3f");

        ImGui::TextDisabled("Flicker suppression");
        physicsDirty |= ImGui::Checkbox("Suppress on sharp turns", &pred.suppressOnTurns);
        if (pred.suppressOnTurns) {
            constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
            constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
            float turnDeg = pred.turnSuppressionThresholdRad * kRadToDeg;
            if (ImGui::SliderFloat("Turn threshold", &turnDeg, 5.0f, 120.0f, "%.0f deg")) {
                pred.turnSuppressionThresholdRad = turnDeg * kDegToRad;
                physicsDirty = true;
            }
        }
        physicsDirty |= ImGui::SliderFloat("Min speed for prediction", &pred.minSpeedMmPerSec,
                                           0.0f, 300.0f, "%.0f /s");
        physicsDirty |= ImGui::Checkbox("Taper predicted tip", &pred.taperTip);
        if (pred.taperTip) {
            physicsDirty |= ImGui::SliderFloat("Tip taper ratio", &pred.tipTaperRatio,
                                               0.0f, 1.0f, "%.2f");
        }
    }

    ImGui::SeparatorText("Angle Upsampling");
    physicsDirty |= ImGui::Checkbox("Enable Angle Upsampling", &config.physics.enableAngleUpsampling);
    if (config.physics.enableAngleUpsampling) {
        constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
        constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
        float angleDeg = std::clamp(config.physics.maxEstimatedAngleRad * kRadToDeg, 2.0f, 30.0f);
        if (ImGui::SliderFloat("max_estimated_angle_to_traverse_per_input", &angleDeg, 2.0f, 30.0f, "%.1f deg")) {
            config.physics.maxEstimatedAngleRad = angleDeg * kDegToRad;
            physicsDirty = true;
        }
    }

    ImGui::SeparatorText("Visual Overlays");
    ImGui::Checkbox("Raw Input Points (Red Dots r=2px)", &m_showRawPoints);
    ImGui::Checkbox("Modeled Centerline (Yellow 2px Polyline)", &m_showModeledCenterline);
    ImGui::Checkbox("Kalman Predicted Spine (Cyan Dotted Trail)", &m_showKalmanSpine);

    return physicsDirty;
}

void ModelerInspector::RenderOverlays(ImDrawList* drawList,
                                      std::span<const RawInputSample> rawSamples,
                                      std::span<const StrokePoint> modeledPoints,
                                      std::span<const StrokePoint> predictedPoints,
                                      const CanvasTransform& transform) const {
    if (!drawList) return;

    // 1. Modeled Centerline: Bright yellow polyline (2px) connecting Google Stroke Modeler output coordinates
    if (m_showModeledCenterline && modeledPoints.size() >= 2) {
        const ImU32 yellowCol = IM_COL32(255, 235, 40, 240);
        for (size_t i = 1; i < modeledPoints.size(); ++i) {
            ImVec2 p0 = transform.WorldToScreen(static_cast<float>(modeledPoints[i - 1].x),
                                                static_cast<float>(modeledPoints[i - 1].y));
            ImVec2 p1 = transform.WorldToScreen(static_cast<float>(modeledPoints[i].x),
                                                static_cast<float>(modeledPoints[i].y));
            drawList->AddLine(p0, p1, yellowCol, 2.0f);
        }
    }

    // 2. Raw Input Points: Red dots (r = 2.0px)
    if (m_showRawPoints && !rawSamples.empty()) {
        const ImU32 redDotCol = IM_COL32(255, 55, 55, 245);
        for (const auto& s : rawSamples) {
            ImVec2 pt = transform.WorldToScreen(s.x, s.y);
            drawList->AddCircleFilled(pt, 2.0f, redDotCol, 12);
        }
    }

    // 3. Kalman Predicted Spine: Cyan dotted trail showing extrapolated lookahead points
    if (m_showKalmanSpine && !predictedPoints.empty()) {
        const ImU32 cyanDotCol = IM_COL32(0, 240, 255, 245);
        const ImU32 cyanLineCol = IM_COL32(0, 220, 245, 160);

        for (size_t i = 1; i < predictedPoints.size(); ++i) {
            ImVec2 p0 = transform.WorldToScreen(static_cast<float>(predictedPoints[i - 1].x),
                                                static_cast<float>(predictedPoints[i - 1].y));
            ImVec2 p1 = transform.WorldToScreen(static_cast<float>(predictedPoints[i].x),
                                                static_cast<float>(predictedPoints[i].y));
            // Draw dotted segments along the prediction spine
            float dx = p1.x - p0.x;
            float dy = p1.y - p0.y;
            float dist = std::hypot(dx, dy);
            if (dist > 1.0f) {
                int steps = std::max(1, static_cast<int>(dist / 6.0f));
                for (int s = 0; s < steps; ++s) {
                    float t0 = static_cast<float>(s) / static_cast<float>(steps);
                    float t1 = std::min(1.0f, (static_cast<float>(s) + 0.5f) / static_cast<float>(steps));
                    drawList->AddLine(
                        ImVec2(p0.x + dx * t0, p0.y + dy * t0),
                        ImVec2(p0.x + dx * t1, p0.y + dy * t1),
                        cyanLineCol, 1.5f);
                }
            }
        }

        for (const auto& pt : predictedPoints) {
            ImVec2 sp = transform.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
            drawList->AddCircleFilled(sp, 2.5f, cyanDotCol, 12);
        }
    }
}

} // namespace Folio::Debug
