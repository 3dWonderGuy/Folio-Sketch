#include "debug/width_inspector.hpp"
#include "folio_sketch/stroke_width_evaluator.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Folio::Debug {

bool WidthInspector::RenderUI(InkingConfig& config,
                              StrokeStyle& style,
                              std::span<const StrokePoint> modeledPoints) {
    bool widthDirty = false;

    ImGui::SeparatorText("Dynamic Inputs");
    if (ImGui::Checkbox("usePressure", &config.usePressure)) {
        style.usePressure = config.usePressure;
        widthDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("useVelocity", &config.useVelocity)) {
        style.useVelocity = config.useVelocity;
        widthDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("useTilt", &config.useTilt)) {
        style.useTilt = config.useTilt;
        widthDirty = true;
    }

    ImGui::SeparatorText("Thickness & Ratio Bounds");
    widthDirty |= ImGui::SliderFloat("baseWidth", &style.baseWidth, 0.5f, 40.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("minWidthRatio", &config.minWidthMultiplier, 0.05f, 1.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("maxWidthRatio", &config.maxWidthMultiplier, 0.5f, 3.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("velocityMaxClamp", &config.velocityThinningMaxSpeed, 50.0f, 3500.0f, "%.0f");
    widthDirty |= ImGui::SliderFloat("EMA Smoothing Factor", &config.dynamicWidthSmoothing, 0.01f, 1.0f, "%.2f");
    widthDirty |= ImGui::SliderFloat("Pressure Gamma", &config.pressureGamma, 0.25f, 3.0f, "%.2f");

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
    const bool isPencil = (style.pattern == StrokePattern::TexturedPencil);
    constexpr float kDefaultDtSec = 1.0f / 180.0f;

    for (size_t i = 0; i < points.size(); ++i) {
        float speed = 0.0f;
        if (i > 0) {
            float ds = static_cast<float>(std::hypot(points[i].x - points[i - 1].x,
                                                     points[i].y - points[i - 1].y));
            speed = ds / kDefaultDtSec;
        }
        float pForEval = config.usePressure ? points[i].pressure : -1.0f;
        float vForEval = config.useVelocity ? speed : 0.0f;
        float tiltRad = config.useTilt ? std::hypot(points[i].tiltX, points[i].tiltY) : 0.0f;

        float width = StrokeWidthEvaluator::CalculateDynamicWidthWithTiltRad(
            style.baseWidth, pForEval, vForEval, tiltRad,
            prevWidth, zoomScale, config, isPencil);
        prevWidth = width;
        points[i].r = std::max(0.01f, width * 0.5f);
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
