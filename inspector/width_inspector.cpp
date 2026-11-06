#include "inspector/width_inspector.hpp"
#include "folio_sketch/engine/stroke_width_evaluator.hpp"
#include "folio_sketch/engine/stroke_post_processor.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Folio::Debug {

bool WidthInspector::RenderUI(InkingConfig& config,
                              StrokeStyle& style,
                              std::span<const StrokePoint> modeledPoints) {
    bool widthDirty = false;

    ImGui::SeparatorText("Dynamic Inputs");
    if (ImGui::Checkbox("usePressure", &config.dynamics.usePressure)) {
        style.usePressure = config.dynamics.usePressure;
        widthDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("useVelocity", &config.dynamics.useVelocity)) {
        style.useVelocity = config.dynamics.useVelocity;
        widthDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("useTilt", &config.dynamics.useTilt)) {
        style.useTilt = config.dynamics.useTilt;
        widthDirty = true;
    }

    ImGui::SeparatorText("Thickness & Ratio Bounds");
    widthDirty |= ImGui::SliderFloat("baseWidth", &style.baseWidth, 0.5f, 60.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("minWidthRatio", &config.dynamics.minWidthMultiplier, 0.05f, 1.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("maxWidthRatio", &config.dynamics.maxWidthMultiplier, 0.5f, 3.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("velocityMaxClamp (mm/s)", &config.dynamics.velocityThinningMaxSpeed, 50.0f, 3500.0f, "%.0f");
    widthDirty |= ImGui::SliderFloat("Overall Width EMA", &config.dynamics.widthSmoothingEma, 0.01f, 1.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("Velocity Thinning EMA (Noise Filter)", &config.dynamics.velocitySmoothingEma, 0.01f, 1.0f, "%.2f");
    widthDirty |= ImGui::Checkbox("enablePressureCurve", &config.calibration.enablePressureCurve);
    widthDirty |= ImGui::SliderFloat("Pressure Gamma", &config.calibration.pressureGamma, 0.25f, 3.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("Pressure Deadzone", &config.calibration.pressureDeadzoneMin, 0.0f, 0.25f, "%.3f");
    widthDirty |= ImGui::SliderFloat("Pressure Saturation", &config.calibration.pressureSaturationMax, 0.50f, 1.0f, "%.3f");

    ImGui::SeparatorText("Spatial Slope Clamping & Blooming Prevention");
    widthDirty |= ImGui::Checkbox("Enable Spatial Slope Clamping (dr/ds)", &config.dynamics.enableSpatialSlopeClamping);
    if (config.dynamics.enableSpatialSlopeClamping) {
        widthDirty |= ImGui::SliderFloat("maxRadiusSlopePerMm", &config.dynamics.maxRadiusSlopePerMm, 0.05f, 10.0f, "%.2f mm");
        ImGui::TextDisabled("Lower (0.1-0.35) prevents blooming when stationary; Higher (>2.0) allows instant pressure pulses.");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Spatial slope clamping disabled: Instantaneous pressure pulses active!");
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
        config.dynamics.velocitySmoothingEma = 0.25f;
        widthDirty = true;
    }
    widthDirty |= ImGui::Checkbox("useAccelerationFiltering", &config.dynamics.useAccelerationFiltering);
    ImGui::SameLine();
    widthDirty |= ImGui::Checkbox("useCornerCompensation", &config.dynamics.useCornerCompensation);
    widthDirty |= ImGui::SliderFloat("maxAcceleration (mm/s^2)", &config.dynamics.maxAccelerationMmPerSec2, 500.0f, 10000.0f, "%.0f");
    widthDirty |= ImGui::SliderFloat("maxDeceleration (mm/s^2)", &config.dynamics.maxDecelerationMmPerSec2, 500.0f, 10000.0f, "%.0f");
    widthDirty |= ImGui::SliderFloat("cornerThresholdRad", &config.dynamics.cornerCompensationThresholdRad, 0.1f, 1.57f, "%.2f rad");

    ImGui::SeparatorText("Gaussian Peak Smoothing (Rounds Sharp Spikes)");
    widthDirty |= ImGui::Checkbox("enableGaussianRadiusSmoothing", &config.dynamics.enableGaussianRadiusSmoothing);
    if (config.dynamics.enableGaussianRadiusSmoothing) {
        widthDirty |= ImGui::SliderFloat("Gaussian Sigma (mm)", &config.dynamics.radiusSmoothingKernelSigmaMm, 0.5f, 15.0f, "%.1f mm");
        widthDirty |= ImGui::SliderInt("Smoothing Passes", &config.dynamics.radiusSmoothingPasses, 1, 5);
        ImGui::TextDisabled("Filters raw width along stroke arc-length to round sharp peaks into smooth hills.");
    }

    ImGui::SeparatorText("Tilt Dynamics (Pencil Shading)");
    {
        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        widthDirty |= ImGui::SliderFloat("maxTiltWidthMultiplier", &config.dynamics.maxTiltWidthMultiplier, 1.0f, 6.0f, "%.2fx");
        float deadDeg = config.dynamics.tiltDeadzoneRad * kRadToDeg;
        if (ImGui::SliderFloat("tiltDeadzone", &deadDeg, 0.0f, 45.0f, "%.1f deg")) {
            config.dynamics.tiltDeadzoneRad = deadDeg * kDegToRad;
            widthDirty = true;
        }
        float maxDeg = config.dynamics.tiltMaxAngleRad * kRadToDeg;
        if (ImGui::SliderFloat("tiltMaxAngle", &maxDeg, 20.0f, 89.0f, "%.1f deg")) {
            config.dynamics.tiltMaxAngleRad = maxDeg * kDegToRad;
            widthDirty = true;
        }
    }

    ImGui::SeparatorText("Tail Taper");
    widthDirty |= ImGui::SliderFloat("tailTaperLength (mm)", &config.dynamics.tailTaperLengthMm, 0.0f, 200.0f, "%.1f mm");
    if (config.dynamics.tailTaperLengthMm > 0.0f) {
        widthDirty |= ImGui::SliderFloat("tailTaperPower", &config.dynamics.tailTaperPower, 0.2f, 6.0f, "%.2f");
    }

    ImGui::SeparatorText("Width Overlays & Curve Plot");
    ImGui::Checkbox("Show Hollow Radius Discs (r_i)", &m_showRadiusDiscs);

    // Compute arc-length parameterized radius profile for ImGui::PlotLines
    m_arcLengthSamples.clear();
    m_arcLengthsMm.clear();

    float totalArcLen = 0.0f;
    float minR = 0.0f;
    float maxR = 1.0f;

    if (!modeledPoints.empty()) {
        minR = modeledPoints[0].r;
        maxR = modeledPoints[0].r;
        m_arcLengthsMm.reserve(modeledPoints.size());
        m_arcLengthsMm.push_back(0.0f);

        for (size_t i = 1; i < modeledPoints.size(); ++i) {
            float ds = static_cast<float>(std::hypot(modeledPoints[i].x - modeledPoints[i - 1].x,
                                                     modeledPoints[i].y - modeledPoints[i - 1].y));
            totalArcLen += ds;
            m_arcLengthsMm.push_back(totalArcLen);
            minR = std::min(minR, modeledPoints[i].r);
            maxR = std::max(maxR, modeledPoints[i].r);
        }

        // Uniformly sample radius across stroke arc-length (128 bins) so X-axis is true arc-length
        constexpr int kPlotBins = 128;
        if (totalArcLen > 1e-4f && modeledPoints.size() >= 2) {
            m_arcLengthSamples.resize(kPlotBins);
            size_t segIdx = 0;
            for (int b = 0; b < kPlotBins; ++b) {
                float targetS = (static_cast<float>(b) / static_cast<float>(kPlotBins - 1)) * totalArcLen;
                while (segIdx + 1 < m_arcLengthsMm.size() && m_arcLengthsMm[segIdx + 1] < targetS) {
                    ++segIdx;
                }
                if (segIdx + 1 < modeledPoints.size()) {
                    float s0 = m_arcLengthsMm[segIdx];
                    float s1 = m_arcLengthsMm[segIdx + 1];
                    float t = (s1 - s0 > 1e-5f) ? std::clamp((targetS - s0) / (s1 - s0), 0.0f, 1.0f) : 0.0f;
                    m_arcLengthSamples[b] = modeledPoints[segIdx].r + t * (modeledPoints[segIdx + 1].r - modeledPoints[segIdx].r);
                } else {
                    m_arcLengthSamples[b] = modeledPoints.back().r;
                }
            }
        } else {
            for (const auto& pt : modeledPoints) {
                m_arcLengthSamples.push_back(pt.r);
            }
        }
    }

    if (!m_arcLengthSamples.empty()) {
        char overlayBuf[96];
        std::snprintf(overlayBuf, sizeof(overlayBuf),
                      "r in [%.2f, %.2f] | ArcLen: %.1f", minR, maxR, totalArcLen);
        float plotMax = std::max(maxR * 1.15f, 0.5f);
        ImGui::PlotLines("Radius vs Arc-Length",
                         m_arcLengthSamples.data(),
                         static_cast<int>(m_arcLengthSamples.size()),
                         0,
                         overlayBuf,
                         0.0f,
                         plotMax,
                         ImVec2(0.0f, 90.0f));
    } else {
        const float zeroVal = 0.0f;
        ImGui::PlotLines("Radius vs Arc-Length", &zeroVal, 1, 0, "No stroke data", 0.0f, 1.0f, ImVec2(0.0f, 90.0f));
    }

    return widthDirty;
}

void WidthInspector::ReevaluateRadii(std::vector<StrokePoint>& points,
                                     const InkingConfig& config,
                                     const StrokeStyle& style,
                                     float zoomScale) {
    if (points.empty()) return;

    float prevWidth = -1.0f;
    float filteredSpeed = 0.0f;
    float lastHeading = 0.0f;
    bool hasHeading = false;
    const bool isPencil = (style.pattern == StrokePattern::TexturedPencil);
    constexpr float kDefaultDtSec = 1.0f / 180.0f;

    for (size_t i = 0; i < points.size(); ++i) {
        float rawSpeed = 0.0f;
        float ds = 0.0f;
        float turnAngleRad = 0.0f;

        if (i > 0) {
            float dx = static_cast<float>(points[i].x - points[i - 1].x);
            float dy = static_cast<float>(points[i].y - points[i - 1].y);
            ds = std::hypot(dx, dy);
            rawSpeed = ds / kDefaultDtSec;

            if (ds > 0.05f) {
                float heading = std::atan2(dy, dx);
                if (hasHeading) {
                    float diff = std::abs(heading - lastHeading);
                    constexpr float kPi = 3.14159265358979323846f;
                    while (diff > kPi) diff = std::abs(diff - 2.0f * kPi);
                    turnAngleRad = diff;
                }
                lastHeading = heading;
                hasHeading = true;
            }
        }

        if (config.dynamics.useAccelerationFiltering) {
            filteredSpeed = StrokeWidthEvaluator::FilterSpeedWithAcceleration(
                rawSpeed, filteredSpeed, kDefaultDtSec,
                config.dynamics.maxAccelerationMmPerSec2,
                config.dynamics.maxDecelerationMmPerSec2);
        } else {
            filteredSpeed = rawSpeed;
        }

        float pForEval = config.dynamics.usePressure ? points[i].pressure : -1.0f;
        float vForEval = config.dynamics.useVelocity ? filteredSpeed : 0.0f;
        float tiltRad = config.dynamics.useTilt ? std::hypot(points[i].tiltX, points[i].tiltY) : 0.0f;

        float width = StrokeWidthEvaluator::CalculateDynamicWidthWithTiltRad(
            style.baseWidth, pForEval, vForEval, tiltRad,
            prevWidth, zoomScale, config, isPencil, turnAngleRad);
        prevWidth = width;

        float targetRadius = std::max(0.01f, width * 0.5f);
        if (i > 0) {
            targetRadius = StrokeWidthEvaluator::ClampSpatialRadiusSlope(
                targetRadius, points[i - 1].r, ds, config.dynamics.maxRadiusSlopePerMm);
        }
        points[i].r = targetRadius;
    }

    if (config.dynamics.enableGaussianRadiusSmoothing && points.size() > 2) {
        StrokePostProcessor::SmoothRadiiGaussian(
            points,
            config.dynamics.radiusSmoothingKernelSigmaMm,
            config.dynamics.radiusSmoothingPasses);
    }
}

void WidthInspector::RenderOverlays(ImDrawList* drawList,
                                    std::span<const StrokePoint> modeledPoints,
                                    const CanvasTransform& transform) const {
    if (!drawList || !m_showRadiusDiscs || modeledPoints.empty()) return;

    const ImU32 discCol = IM_COL32(255, 165, 60, 175);
    for (const auto& pt : modeledPoints) {
        ImVec2 center = transform.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
        float rScreen = std::max(1.0f, pt.r * transform.zoom);
        drawList->AddCircle(center, rScreen, discCol, 24, 1.0f);
    }
}

} // namespace Folio::Debug
