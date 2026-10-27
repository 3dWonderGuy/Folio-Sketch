#include "debug/stroke_scrubber.hpp"
#include "folio_sketch/ink_engine.hpp"
#include "folio_sketch/ink_generator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Folio::Debug {

void StrokeScrubber::FreezeStroke(const std::vector<RawInputSample>& rawSamples,
                                  const std::vector<StrokePoint>& modeledPoints,
                                  const StrokeStyle& style,
                                  float zoomScale) {
    m_frozenRawSamples = rawSamples;
    m_frozenModeledPoints = modeledPoints;
    m_cursor = static_cast<int>(m_frozenModeledPoints.size());
    m_isFrozen = true;
    Retessellate(style, zoomScale);
}

void StrokeScrubber::LoadRepro114(StrokeStyle& style,
                                  std::vector<RawInputSample>& outRawSamples,
                                  CanvasTransform* optionalViewportToCenter) {
    static const std::vector<StrokePoint> kRepro114Points = {
#include "../../test/repro114.inc"
    };

    m_frozenModeledPoints = kRepro114Points;
    m_frozenRawSamples.clear();
    m_frozenRawSamples.reserve(kRepro114Points.size());

    double t = 0.0;
    for (const auto& pt : kRepro114Points) {
        m_frozenRawSamples.push_back(RawInputSample{
            .x = static_cast<float>(pt.x),
            .y = static_cast<float>(pt.y),
            .pressure = pt.pressure,
            .tiltRad = std::hypot(pt.tiltX, pt.tiltY),
            .timestampSec = t
        });
        t += 1.0 / 240.0;
    }
    outRawSamples = m_frozenRawSamples;

    style.baseWidth = 25.0f; // repro114 has r = 12.5f -> width = 25.0f
    style.capType = CapType::Round;
    style.pattern = StrokePattern::Solid;

    // Position the scrubber directly at the tight crescent turn defect region (around index 125 of 133)
    const int total = static_cast<int>(m_frozenModeledPoints.size());
    m_cursor = std::min(total, std::max(1, 125));
    m_isFrozen = true;

    if (optionalViewportToCenter && !m_frozenModeledPoints.empty()) {
        double minX = m_frozenModeledPoints[0].x, maxX = m_frozenModeledPoints[0].x;
        double minY = m_frozenModeledPoints[0].y, maxY = m_frozenModeledPoints[0].y;
        for (const auto& p : m_frozenModeledPoints) {
            minX = std::min(minX, p.x - p.r);
            maxX = std::max(maxX, p.x + p.r);
            minY = std::min(minY, p.y - p.r);
            maxY = std::max(maxY, p.y + p.r);
        }
        const float cx = static_cast<float>((minX + maxX) * 0.5);
        const float cy = static_cast<float>((minY + maxY) * 0.5);
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float screenCx = vp ? (vp->WorkSize.x * 0.60f) : 960.0f;
        const float screenCy = vp ? (vp->WorkSize.y * 0.50f) : 540.0f;
        optionalViewportToCenter->zoom = 4.5f;
        optionalViewportToCenter->panX = screenCx - cx * optionalViewportToCenter->zoom;
        optionalViewportToCenter->panY = screenCy - cy * optionalViewportToCenter->zoom;
    }

    Retessellate(style, optionalViewportToCenter ? optionalViewportToCenter->zoom : 1.0f);
}

void StrokeScrubber::Retessellate(const StrokeStyle& style, float zoomScale) {
    m_scrubbedMesh.clear();
    m_scrubbedOutline.clear();

    auto slice = GetActiveModeledSlice();
    if (slice.empty()) {
        return;
    }

    std::vector<StrokePoint> subPoints(slice.begin(), slice.end());
    m_scrubbedOutline = InkEngine::BuildStrokeOutline(subPoints, style.capType, style.pattern);
    InkEngine::BuildStrokeMesh(
        slice,
        style.capType,
        PackGpuColor(style.color),
        m_scrubbedMesh,
        std::max(0.5f, zoomScale),
        style.pattern);
}

bool StrokeScrubber::RenderUI(StrokeStyle& style,
                              const std::vector<RawInputSample>& currentRawSamples,
                              const std::vector<StrokePoint>& currentModeledPoints,
                              std::vector<RawInputSample>& mutableRawSamples,
                              CanvasTransform& viewport,
                              float zoomScale) {
    bool changed = false;

    // Keyboard Hotkey: Spacebar toggles Freeze / Unfreeze (when not typing in a text widget)
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (!m_isFrozen) {
            FreezeStroke(currentRawSamples, currentModeledPoints, style, zoomScale);
        } else {
            m_isFrozen = false;
        }
        changed = true;
    }

    ImGui::SeparatorText("Freeze & Capture Controls");
    if (!m_isFrozen) {
        if (ImGui::Button("Freeze Last Stroke [Space]")) {
            FreezeStroke(currentRawSamples, currentModeledPoints, style, zoomScale);
            changed = true;
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.28f, 0.22f, 1.0f));
        if (ImGui::Button("Unfreeze (Resume Live Drawing) [Space]")) {
            m_isFrozen = false;
            changed = true;
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-Freeze on Lift", &m_autoFreezeOnLift);

    // Repro Injector button
    ImGui::SeparatorText("Headless Repro Injector");
    if (ImGui::Button("Load repro114.inc")) {
        LoadRepro114(style, mutableRawSamples, &viewport);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(133 pts crescent turnaround defect)");

    // Step Controls
    ImGui::SeparatorText("Time-Travel Point Scrubber");
    const int totalPts = static_cast<int>(m_frozenModeledPoints.size());
    m_cursor = std::clamp(m_cursor, 0, totalPts);

    if (ImGui::SliderInt("Point Cursor (0 .. N)", &m_cursor, 0, totalPts)) {
        m_isFrozen = true;
        Retessellate(style, zoomScale);
        changed = true;
    }

    // Navigation buttons + hotkeys '[' and ']'
    const bool stepBackKey = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true);
    const bool stepFwdKey  = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_RightBracket, true);

    if (ImGui::Button("|< First")) {
        m_isFrozen = true;
        m_cursor = (totalPts > 0) ? 1 : 0;
        Retessellate(style, zoomScale);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Step Back ([)") || stepBackKey) {
        if (totalPts > 0) {
            m_isFrozen = true;
            m_cursor = std::max(0, m_cursor - 1);
            Retessellate(style, zoomScale);
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Forward > (])") || stepFwdKey) {
        if (totalPts > 0) {
            m_isFrozen = true;
            m_cursor = std::min(totalPts, m_cursor + 1);
            Retessellate(style, zoomScale);
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Last >|")) {
        if (totalPts > 0) {
            m_isFrozen = true;
            m_cursor = totalPts;
            Retessellate(style, zoomScale);
            changed = true;
        }
    }

    if (m_cursor > 0 && m_cursor <= totalPts) {
        const auto& curPt = m_frozenModeledPoints[static_cast<size_t>(m_cursor - 1)];
        ImGui::Text("Cursor Pt [%d / %d]: pos=(%.2f, %.2f)  r=%.3f  p=%.3f",
                    m_cursor, totalPts, curPt.x, curPt.y, curPt.r, curPt.pressure);
    } else {
        ImGui::Text("Cursor Pt [0 / %d]: (empty slice)", totalPts);
    }

    return changed;
}

void StrokeScrubber::RenderCursorHighlight(ImDrawList* drawList, const CanvasTransform& transform) const {
    if (!drawList || !m_isFrozen || m_cursor <= 0 || m_frozenModeledPoints.empty()) return;

    const size_t idx = std::min(static_cast<size_t>(m_cursor - 1), m_frozenModeledPoints.size() - 1);
    const auto& pt = m_frozenModeledPoints[idx];
    ImVec2 screenPos = transform.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));

    drawList->AddCircle(screenPos, std::max(6.0f, pt.r * transform.zoom + 3.0f),
                        IM_COL32(255, 255, 255, 230), 24, 2.0f);
    drawList->AddCircleFilled(screenPos, 3.5f, IM_COL32(255, 80, 220, 255), 12);
}

} // namespace Folio::Debug
