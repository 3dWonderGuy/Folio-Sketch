#include "debug/raw_sensor_layer.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio::Debug {

void RawSensorHUD::RecordTimestamp(double nowSec) {
    if (m_lastTimestampSec > 0.0 && nowSec >= m_lastTimestampSec) {
        m_deltaTimeMs = static_cast<float>((nowSec - m_lastTimestampSec) * 1000.0);
    }
    m_lastTimestampSec = nowSec;

    m_rollingTimestampsSec.push_back(nowSec);
    // Prune timestamps older than 1.0 second
    while (!m_rollingTimestampsSec.empty() && (nowSec - m_rollingTimestampsSec.front()) > 1.0) {
        m_rollingTimestampsSec.pop_front();
    }

    if (m_rollingTimestampsSec.size() >= 2) {
        double span = m_rollingTimestampsSec.back() - m_rollingTimestampsSec.front();
        if (span > 1e-4) {
            m_pollingRateHz = static_cast<float>((m_rollingTimestampsSec.size() - 1) / std::max(0.15, span));
        } else {
            m_pollingRateHz = static_cast<float>(m_rollingTimestampsSec.size());
        }
    } else {
        m_pollingRateHz = static_cast<float>(m_rollingTimestampsSec.size());
    }
}

bool RawSensorHUD::HandleEvent(const SDL_Event& event,
                               const CanvasTransform& transform,
                               RawInputSample& outSample,
                               int& outEventType) {
    outEventType = 0;
    const double nowSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;

    // 1. Stylus Pen Axis Telemetry (Pressure, Tilt X, Tilt Y)
    if (event.type == SDL_EVENT_PEN_AXIS) {
        constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
        if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
            m_currentPressure = std::clamp(event.paxis.value, 0.0f, 1.0f);
        } else if (event.paxis.axis == SDL_PEN_AXIS_XTILT) {
            m_tiltXRad = std::clamp(event.paxis.value * kDegToRad, -1.570796f, 1.570796f);
            m_currentTiltRad = std::hypot(m_tiltXRad, m_tiltYRad);
        } else if (event.paxis.axis == SDL_PEN_AXIS_YTILT) {
            m_tiltYRad = std::clamp(event.paxis.value * kDegToRad, -1.570796f, 1.570796f);
            m_currentTiltRad = std::hypot(m_tiltXRad, m_tiltYRad);
        }
        return true;
    }

    // 2. Stylus Pen Down / Motion / Up
    if (event.type == SDL_EVENT_PEN_DOWN) {
        ImVec2 worldPt = transform.ScreenToWorld(event.ptouch.x, event.ptouch.y);
        m_currentX = worldPt.x;
        m_currentY = worldPt.y;
        if (m_currentPressure <= 0.001f) {
            m_currentPressure = 0.5f;
        }
        m_penDown = true;
        RecordTimestamp(nowSec);

        outSample = RawInputSample{
            .x = m_currentX,
            .y = m_currentY,
            .pressure = m_currentPressure,
            .tiltRad = m_currentTiltRad,
            .timestampSec = nowSec
        };
        m_activeSamples.clear();
        m_activeSamples.push_back(outSample);
        outEventType = 1;
        return true;
    }

    if (event.type == SDL_EVENT_PEN_MOTION) {
        ImVec2 worldPt = transform.ScreenToWorld(event.pmotion.x, event.pmotion.y);
        m_currentX = worldPt.x;
        m_currentY = worldPt.y;
        RecordTimestamp(nowSec);

        if (m_penDown) {
            outSample = RawInputSample{
                .x = m_currentX,
                .y = m_currentY,
                .pressure = m_currentPressure,
                .tiltRad = m_currentTiltRad,
                .timestampSec = nowSec
            };
            m_activeSamples.push_back(outSample);
            outEventType = 2;
        }
        return true;
    }

    if (event.type == SDL_EVENT_PEN_UP) {
        ImVec2 worldPt = transform.ScreenToWorld(event.ptouch.x, event.ptouch.y);
        m_currentX = worldPt.x;
        m_currentY = worldPt.y;
        RecordTimestamp(nowSec);

        if (m_penDown) {
            m_penDown = false;
            outSample = RawInputSample{
                .x = m_currentX,
                .y = m_currentY,
                .pressure = 0.0f,
                .tiltRad = m_currentTiltRad,
                .timestampSec = nowSec
            };
            m_activeSamples.push_back(outSample);
            m_lastStrokeSamples = m_activeSamples;
            m_activeSamples.clear();
            m_currentPressure = 0.0f;
            outEventType = 3;
        }
        return true;
    }

    // 3. Mouse Input Fallback (Left Button = Inking when Pen is not active)
    if (!m_penDown) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            ImVec2 worldPt = transform.ScreenToWorld(event.button.x, event.button.y);
            m_currentX = worldPt.x;
            m_currentY = worldPt.y;
            m_currentPressure = 0.65f;
            m_currentTiltRad = 0.0f;
            m_tiltXRad = 0.0f;
            m_tiltYRad = 0.0f;
            m_mouseDown = true;
            RecordTimestamp(nowSec);

            outSample = RawInputSample{
                .x = m_currentX,
                .y = m_currentY,
                .pressure = m_currentPressure,
                .tiltRad = m_currentTiltRad,
                .timestampSec = nowSec
            };
            m_activeSamples.clear();
            m_activeSamples.push_back(outSample);
            outEventType = 1;
            return true;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            ImVec2 worldPt = transform.ScreenToWorld(event.motion.x, event.motion.y);
            m_currentX = worldPt.x;
            m_currentY = worldPt.y;
            RecordTimestamp(nowSec);

            if (m_mouseDown) {
                // Simulate subtle pressure modulation with speed during mouse drawing for testing
                float speedPx = std::hypot(event.motion.xrel, event.motion.yrel);
                m_currentPressure = std::clamp(0.75f - speedPx * 0.006f, 0.20f, 0.95f);
                outSample = RawInputSample{
                    .x = m_currentX,
                    .y = m_currentY,
                    .pressure = m_currentPressure,
                    .tiltRad = m_currentTiltRad,
                    .timestampSec = nowSec
                };
                m_activeSamples.push_back(outSample);
                outEventType = 2;
            }
            return true;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            ImVec2 worldPt = transform.ScreenToWorld(event.button.x, event.button.y);
            m_currentX = worldPt.x;
            m_currentY = worldPt.y;
            RecordTimestamp(nowSec);

            if (m_mouseDown) {
                m_mouseDown = false;
                outSample = RawInputSample{
                    .x = m_currentX,
                    .y = m_currentY,
                    .pressure = 0.0f,
                    .tiltRad = m_currentTiltRad,
                    .timestampSec = nowSec
                };
                m_activeSamples.push_back(outSample);
                m_lastStrokeSamples = m_activeSamples;
                m_activeSamples.clear();
                m_currentPressure = 0.0f;
                outEventType = 3;
            }
            return true;
        }
    }

    return false;
}

void RawSensorHUD::Render(ImDrawList* drawList) {
    CanvasTransform identity{};
    Render(drawList, identity);
}

void RawSensorHUD::Render(ImDrawList* drawList, const CanvasTransform& transform) {
    if (!drawList) return;

    // 1. Raw Hardware Input Crosshairs & Thin 1px Gray Connecting Lines
    if (m_showRawCrosshairs) {
        const auto& samples = GetActiveOrLastStroke();
        if (!samples.empty()) {
            const ImU32 lineCol = IM_COL32(140, 145, 155, 190);
            const ImU32 crossCol = IM_COL32(255, 65, 65, 235);
            constexpr float crossHalf = 3.5f;

            for (size_t i = 1; i < samples.size(); ++i) {
                ImVec2 p0 = transform.WorldToScreen(samples[i - 1].x, samples[i - 1].y);
                ImVec2 p1 = transform.WorldToScreen(samples[i].x, samples[i].y);
                drawList->AddLine(p0, p1, lineCol, 1.0f);
            }

            for (const auto& s : samples) {
                ImVec2 pt = transform.WorldToScreen(s.x, s.y);
                drawList->AddLine(ImVec2(pt.x - crossHalf, pt.y), ImVec2(pt.x + crossHalf, pt.y), crossCol, 1.0f);
                drawList->AddLine(ImVec2(pt.x, pt.y - crossHalf), ImVec2(pt.x, pt.y + crossHalf), crossCol, 1.0f);
            }
        }
    }

    // 2. Screen Corner HUD Overlay (Top-Right Corner)
    if (!m_showHudOverlay) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = 14.0f;
    const float boxW = 275.0f;
    const float boxH = 148.0f;
    const ImVec2 boxMin(vp->WorkPos.x + vp->WorkSize.x - boxW - pad, vp->WorkPos.y + pad);
    const ImVec2 boxMax(boxMin.x + boxW, boxMin.y + boxH);

    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(18, 20, 26, 225), 8.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(70, 80, 100, 200), 8.0f, 0, 1.2f);

    char buf[128];
    float yCursor = boxMin.y + 10.0f;
    const float xLeft = boxMin.x + 12.0f;

    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(110, 200, 255, 255), "RAW DIGITIZER TELEMETRY");
    yCursor += 22.0f;

    std::snprintf(buf, sizeof(buf), "Pos (X, Y):  (%.2f, %.2f)", m_currentX, m_currentY);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(235, 238, 245, 255), buf);
    yCursor += 20.0f;

    // Pressure + visual progress bar
    float clampedP = std::clamp(m_currentPressure, 0.0f, 1.0f);
    std::snprintf(buf, sizeof(buf), "Pressure:    %.3f", clampedP);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(235, 238, 245, 255), buf);

    const float barX = xLeft + 132.0f;
    const float barW = boxW - 146.0f;
    const float barH = 12.0f;
    const ImVec2 barMin(barX, yCursor + 2.0f);
    const ImVec2 barMax(barX + barW, yCursor + 2.0f + barH);
    drawList->AddRectFilled(barMin, barMax, IM_COL32(40, 44, 55, 255), 3.0f);
    if (clampedP > 0.001f) {
        ImU32 fillCol = IM_COL32(
            static_cast<int>(60 + 195 * clampedP),
            static_cast<int>(210 - 90 * clampedP),
            95, 255);
        drawList->AddRectFilled(barMin, ImVec2(barX + barW * clampedP, barMax.y), fillCol, 3.0f);
    }
    drawList->AddRect(barMin, barMax, IM_COL32(90, 100, 120, 200), 3.0f);
    yCursor += 20.0f;

    const float tiltDeg = m_currentTiltRad * (180.0f / static_cast<float>(M_PI));
    std::snprintf(buf, sizeof(buf), "Tilt:        %.1f deg (%.2f rad)", tiltDeg, m_currentTiltRad);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(235, 238, 245, 255), buf);
    yCursor += 20.0f;

    std::snprintf(buf, sizeof(buf), "Polling:     %.1f Hz", m_pollingRateHz);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(130, 240, 160, 255), buf);
    yCursor += 20.0f;

    std::snprintf(buf, sizeof(buf), "Delta t:     %.2f ms", m_deltaTimeMs);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(255, 210, 120, 255), buf);
}

bool RawSensorHUD::RenderControls(float baseRadius) {
    bool changed = false;
    changed |= ImGui::Checkbox("Show Corner Sensor HUD", &m_showHudOverlay);
    changed |= ImGui::Checkbox("Draw Raw Hardware Crosshairs & Lines", &m_showRawCrosshairs);

    const auto& stroke = GetActiveOrLastStroke();
    ImGui::Text("Captured Raw Samples: %zu", stroke.size());

    if (ImGui::Button("Dump Stroke to .inc")) {
        if (DumpStrokeToInc("dumped_stroke.inc", baseRadius)) {
            m_lastDumpStatus = "Saved to dumped_stroke.inc (& copied to clipboard)";
        } else {
            m_lastDumpStatus = "No stroke samples available to dump";
        }
        changed = true;
    }
    if (!m_lastDumpStatus.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "%s", m_lastDumpStatus.c_str());
    }
    return changed;
}

bool RawSensorHUD::DumpStrokeToInc(const std::string& filePath, float defaultRadius) {
    const auto& samples = GetActiveOrLastStroke();
    if (samples.empty()) return false;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        float effP = (s.pressure > 0.001f) ? s.pressure : 1.0f;
        float r = (defaultRadius > 0.01f) ? defaultRadius : 12.500000f;
        oss << "    { " << s.x << ", " << s.y << ", "
            << r << "f, " << effP << "f, "
            << s.tiltRad << "f, 0.000000f }";
        if (i + 1 < samples.size()) {
            oss << ",";
        }
        oss << "\n";
    }

    const std::string formatted = oss.str();
    ImGui::SetClipboardText(formatted.c_str());

    std::ofstream ofs(filePath, std::ios::out | std::ios::trunc);
    if (ofs.is_open()) {
        ofs << formatted;
        ofs.close();
    }

    std::printf("// === Dumped Stroke (%zu samples) -> %s ===\n%s",
                samples.size(), filePath.c_str(), formatted.c_str());
    return true;
}

} // namespace Folio::Debug
