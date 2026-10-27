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
    physicsDirty |= ImGui::SliderFloat("spring_constant", &config.google_spring_mass_constant,
                                       0.00005f, 0.00500f, "%.5f");
    physicsDirty |= ImGui::SliderFloat("drag_coefficient", &config.google_drag_constant,
                                       10.0f, 200.0f, "%.1f");

    ImGui::SeparatorText("Wobble Smoother (Tremor Filter)");
    physicsDirty |= ImGui::Checkbox("Wobble is_enabled", &config.google_wobble_enable);
    if (config.google_wobble_enable) {
        physicsDirty |= ImGui::SliderFloat("Wobble timeout (s)", &config.google_wobble_timeout_s,
                                           0.001f, 0.150f, "%.3f s");
    }

    ImGui::SeparatorText("Loop Contraction & Projection");
    if (ImGui::Checkbox("Loop Contraction Mitigation is_enabled", &config.google_loop_mitigation_enable)) {
        if (config.google_loop_mitigation_enable) {
            // Google StrokeModeler validation requires normal projection when loop mitigation is enabled
            config.google_use_stroke_normal_projection = true;
        }
        physicsDirty = true;
    }
    if (ImGui::Checkbox("use_stroke_normal_projection", &config.google_use_stroke_normal_projection)) {
        if (!config.google_use_stroke_normal_projection && config.google_loop_mitigation_enable) {
            config.google_loop_mitigation_enable = false;
        }
        physicsDirty = true;
    }

    ImGui::SeparatorText("Kalman Prediction");
    physicsDirty |= ImGui::Checkbox("google_enable_prediction", &config.google_enable_prediction);
    if (config.google_enable_prediction) {
        physicsDirty |= ImGui::SliderFloat("Prediction Duration (ms)", &config.google_prediction_duration_ms,
                                           0.0f, 50.0f, "%.1f ms");
    }

    ImGui::SeparatorText("Angle Upsampling");
    physicsDirty |= ImGui::Checkbox("Enable Angle Upsampling", &config.google_enable_angle_upsampling);
    if (config.google_enable_angle_upsampling) {
        constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
        constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
        float angleDeg = std::clamp(config.google_max_estimated_angle_rad * kRadToDeg, 2.0f, 30.0f);
        if (ImGui::SliderFloat("max_estimated_angle_to_traverse_per_input", &angleDeg, 2.0f, 30.0f, "%.1f deg")) {
            config.google_max_estimated_angle_rad = angleDeg * kDegToRad;
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
