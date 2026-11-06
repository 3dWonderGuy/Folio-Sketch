#include "inspector/raw_sensor_layer.hpp"
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
    std::vector<RawInputSample> batch;
    bool res = HandleEventBatch(event, transform, batch, outEventType);
    if (!batch.empty()) {
        outSample = batch.back();
    }
    return res;
}

bool RawSensorHUD::HandleEventBatch(const SDL_Event& event,
                                    const CanvasTransform& transform,
                                    std::vector<RawInputSample>& outBatch,
                                    int& outEventType) {
    outEventType = 0;
    outBatch.clear();
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

        RawInputSample sample{
            .x = m_currentX,
            .y = m_currentY,
            .pressure = m_currentPressure,
            .tiltX = m_tiltXRad,
            .tiltY = m_tiltYRad,
            .timeNs = static_cast<int64_t>(nowSec * 1e9),
            .tiltRad = m_currentTiltRad,
            .timestampSec = nowSec
        };
        m_activeSamples.clear();
        m_activeSamples.push_back(sample);
        outBatch.push_back(sample);
        m_prevSample = sample;
        m_hasPrevSample = true;
        m_totalIngestedPackets++;
        m_lastBatchSize = 1;
        outEventType = 1;
        return true;
    }

    if (event.type == SDL_EVENT_PEN_MOTION) {
        ImVec2 worldPt = transform.ScreenToWorld(event.pmotion.x, event.pmotion.y);
        m_currentX = worldPt.x;
        m_currentY = worldPt.y;
        RecordTimestamp(nowSec);

        if (m_penDown) {
            RawInputSample currSample{
                .x = m_currentX,
                .y = m_currentY,
                .pressure = m_currentPressure,
                .tiltX = m_tiltXRad,
                .tiltY = m_tiltYRad,
                .timeNs = static_cast<int64_t>(nowSec * 1e9),
                .tiltRad = m_currentTiltRad,
                .timestampSec = nowSec
            };

            const int K = std::max(1, m_oversampleFactor);
            if (K > 1 && m_hasPrevSample) {
                double dt = nowSec - m_prevSample.timestampSec;
                if (dt <= 0.0) dt = 0.002;

                for (int i = 1; i <= K; ++i) {
                    float alpha = static_cast<float>(i) / static_cast<float>(K);
                    double subTime = m_prevSample.timestampSec + alpha * dt;
                    float subX = m_prevSample.x + alpha * (currSample.x - m_prevSample.x);
                    float subY = m_prevSample.y + alpha * (currSample.y - m_prevSample.y);
                    float subP = m_prevSample.pressure + alpha * (currSample.pressure - m_prevSample.pressure);

                    // Micro-jitter injection for intermediate sub-packets
                    if (m_microJitterMm > 0.0f && i < K) {
                        float jx = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * m_microJitterMm;
                        float jy = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * m_microJitterMm;
                        subX += jx;
                        subY += jy;
                    }

                    // Simulated packet drop
                    if (m_packetDropRate > 0.001f && i < K) {
                        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                        if (r < m_packetDropRate) {
                            m_totalDroppedPackets++;
                            continue;
                        }
                    }

                    RawInputSample subPt{
                        .x = subX,
                        .y = subY,
                        .pressure = subP,
                        .tiltX = m_tiltXRad,
                        .tiltY = m_tiltYRad,
                        .timeNs = static_cast<int64_t>(subTime * 1e9),
                        .tiltRad = m_currentTiltRad,
                        .timestampSec = subTime
                    };
                    outBatch.push_back(subPt);
                    m_activeSamples.push_back(subPt);
                    m_totalIngestedPackets++;
                }
            } else {
                outBatch.push_back(currSample);
                m_activeSamples.push_back(currSample);
                m_totalIngestedPackets++;
            }

            m_prevSample = currSample;
            m_hasPrevSample = true;
            m_lastBatchSize = static_cast<int>(outBatch.size());
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
            m_hasPrevSample = false;
            RawInputSample sample{
                .x = m_currentX,
                .y = m_currentY,
                .pressure = 0.0f,
                .tiltX = m_tiltXRad,
                .tiltY = m_tiltYRad,
                .timeNs = static_cast<int64_t>(nowSec * 1e9),
                .tiltRad = m_currentTiltRad,
                .timestampSec = nowSec
            };
            m_activeSamples.push_back(sample);
            outBatch.push_back(sample);
            m_lastStrokeSamples = m_activeSamples;
            m_activeSamples.clear();
            m_currentPressure = 0.0f;
            m_lastBatchSize = 1;
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

            RawInputSample sample{
                .x = m_currentX,
                .y = m_currentY,
                .pressure = m_currentPressure,
                .tiltX = 0.0f,
                .tiltY = 0.0f,
                .timeNs = static_cast<int64_t>(nowSec * 1e9),
                .tiltRad = m_currentTiltRad,
                .timestampSec = nowSec
            };
            m_activeSamples.clear();
            m_activeSamples.push_back(sample);
            outBatch.push_back(sample);
            m_prevSample = sample;
            m_hasPrevSample = true;
            m_totalIngestedPackets++;
            m_lastBatchSize = 1;
            outEventType = 1;
            return true;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            ImVec2 worldPt = transform.ScreenToWorld(event.motion.x, event.motion.y);
            m_currentX = worldPt.x;
            m_currentY = worldPt.y;
            RecordTimestamp(nowSec);

            if (m_mouseDown) {
                m_currentPressure = 0.65f;
                RawInputSample currSample{
                    .x = m_currentX,
                    .y = m_currentY,
                    .pressure = m_currentPressure,
                    .tiltX = 0.0f,
                    .tiltY = 0.0f,
                    .timeNs = static_cast<int64_t>(nowSec * 1e9),
                    .tiltRad = m_currentTiltRad,
                    .timestampSec = nowSec
                };

                const int K = std::max(1, m_oversampleFactor);
                if (K > 1 && m_hasPrevSample) {
                    double dt = nowSec - m_prevSample.timestampSec;
                    if (dt <= 0.0) dt = 0.002;

                    for (int i = 1; i <= K; ++i) {
                        float alpha = static_cast<float>(i) / static_cast<float>(K);
                        double subTime = m_prevSample.timestampSec + alpha * dt;
                        float subX = m_prevSample.x + alpha * (currSample.x - m_prevSample.x);
                        float subY = m_prevSample.y + alpha * (currSample.y - m_prevSample.y);
                        float subP = m_prevSample.pressure + alpha * (currSample.pressure - m_prevSample.pressure);

                        if (m_microJitterMm > 0.0f && i < K) {
                            float jx = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * m_microJitterMm;
                            float jy = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * m_microJitterMm;
                            subX += jx;
                            subY += jy;
                        }

                        if (m_packetDropRate > 0.001f && i < K) {
                            float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                            if (r < m_packetDropRate) {
                                m_totalDroppedPackets++;
                                continue;
                            }
                        }

                        RawInputSample subPt{
                            .x = subX,
                            .y = subY,
                            .pressure = subP,
                            .tiltX = 0.0f,
                            .tiltY = 0.0f,
                            .timeNs = static_cast<int64_t>(subTime * 1e9),
                            .tiltRad = m_currentTiltRad,
                            .timestampSec = subTime
                        };
                        outBatch.push_back(subPt);
                        m_activeSamples.push_back(subPt);
                        m_totalIngestedPackets++;
                    }
                } else {
                    outBatch.push_back(currSample);
                    m_activeSamples.push_back(currSample);
                    m_totalIngestedPackets++;
                }

                m_prevSample = currSample;
                m_hasPrevSample = true;
                m_lastBatchSize = static_cast<int>(outBatch.size());
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
                m_hasPrevSample = false;
                RawInputSample sample{
                    .x = m_currentX,
                    .y = m_currentY,
                    .pressure = 0.0f,
                    .tiltX = 0.0f,
                    .tiltY = 0.0f,
                    .timeNs = static_cast<int64_t>(nowSec * 1e9),
                    .tiltRad = m_currentTiltRad,
                    .timestampSec = nowSec
                };
                m_activeSamples.push_back(sample);
                outBatch.push_back(sample);
                m_lastStrokeSamples = m_activeSamples;
                m_activeSamples.clear();
                m_currentPressure = 0.0f;
                m_lastBatchSize = 1;
                outEventType = 3;
            }
            return true;
        }
    }

    return false;
}

void RawSensorHUD::UpdateTelemetryTipLag(float rawX, float rawY, float modX, float modY, float predX, float predY) {
    m_liveTipLagMm = std::hypot(rawX - modX, rawY - modY);
    m_livePredLagMm = std::hypot(rawX - predX, rawY - predY);
}

void RawSensorHUD::RecordModelerTimeUs(float timeUs) {
    m_lastModelerTimeUs = timeUs;
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
    const float boxW = 285.0f;
    const float boxH = 210.0f;
    const ImVec2 boxMin(vp->WorkPos.x + vp->WorkSize.x - boxW - pad, vp->WorkPos.y + pad);
    const ImVec2 boxMax(boxMin.x + boxW, boxMin.y + boxH);

    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(18, 20, 26, 230), 8.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(70, 80, 100, 200), 8.0f, 0, 1.2f);

    char buf[128];
    float yCursor = boxMin.y + 10.0f;
    const float xLeft = boxMin.x + 12.0f;

    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(110, 200, 255, 255), "DIGITIZER & MODELER TELEMETRY");
    yCursor += 22.0f;

    std::snprintf(buf, sizeof(buf), "Pos (X, Y):  (%.1f, %.1f)", m_currentX, m_currentY);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(235, 238, 245, 255), buf);
    yCursor += 19.0f;

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
    yCursor += 19.0f;

    const float effectiveHz = (m_oversampleFactor > 1) ? (m_pollingRateHz * m_oversampleFactor) : m_pollingRateHz;
    std::snprintf(buf, sizeof(buf), "Rate:        %.1f Hz (%dx burst, %d pkts)",
                  effectiveHz, m_oversampleFactor, m_lastBatchSize);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(130, 240, 160, 255), buf);
    yCursor += 19.0f;

    std::snprintf(buf, sizeof(buf), "Delta t:     %.2f ms", (m_oversampleFactor > 1 && m_deltaTimeMs > 0.0f)
                  ? (m_deltaTimeMs / m_oversampleFactor) : m_deltaTimeMs);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(255, 210, 120, 255), buf);
    yCursor += 19.0f;

    std::snprintf(buf, sizeof(buf), "Modeler CPU: %.1f us/batch", m_lastModelerTimeUs);
    drawList->AddText(ImVec2(xLeft, yCursor), IM_COL32(200, 160, 255, 255), buf);
    yCursor += 19.0f;

    std::snprintf(buf, sizeof(buf), "Tip Lag:     %.2f mm (%.1f px)",
                  m_liveTipLagMm, m_liveTipLagMm * transform.zoom);
    drawList->AddText(ImVec2(xLeft, yCursor),
                      (m_liveTipLagMm > 4.0f) ? IM_COL32(255, 110, 110, 255) : IM_COL32(120, 220, 255, 255),
                      buf);
    yCursor += 19.0f;

    const uint64_t totalPkts = m_totalIngestedPackets + m_totalDroppedPackets;
    const float retention = (totalPkts > 0) ? (100.0f * m_totalIngestedPackets / totalPkts) : 100.0f;
    std::snprintf(buf, sizeof(buf), "Retention:   %.1f%% (%llu dropped)",
                  retention, static_cast<unsigned long long>(m_totalDroppedPackets));
    drawList->AddText(ImVec2(xLeft, yCursor),
                      (m_totalDroppedPackets > 0) ? IM_COL32(255, 175, 90, 255) : IM_COL32(140, 230, 160, 255),
                      buf);
}

bool RawSensorHUD::RenderControls(float baseRadius) {
    bool changed = false;
    changed |= ImGui::Checkbox("Show Corner Sensor HUD", &m_showHudOverlay);
    changed |= ImGui::Checkbox("Draw Raw Hardware Crosshairs & Lines", &m_showRawCrosshairs);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "High-Frequency Burst Generator (Desktop Simulation):");
    ImGui::TextWrapped("Simulates 240Hz-480Hz digitizer packet bursts by oversampling input events between UI frames.");

    ImGui::Text("Oversample Factor:");
    ImGui::SameLine();
    if (ImGui::RadioButton("1x (Native)", m_oversampleFactor == 1)) { m_oversampleFactor = 1; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("2x (~120Hz)", m_oversampleFactor == 2)) { m_oversampleFactor = 2; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("4x (~240Hz)", m_oversampleFactor == 4)) { m_oversampleFactor = 4; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("8x (~480Hz)", m_oversampleFactor == 8)) { m_oversampleFactor = 8; changed = true; }

    changed |= ImGui::SliderFloat("Sub-Packet Micro-Jitter (mm)", &m_microJitterMm, 0.0f, 0.50f, "%.2f mm");
    changed |= ImGui::SliderFloat("Simulated Hardware Drop Rate", &m_packetDropRate, 0.0f, 0.30f, "%.1f %%");

    ImGui::Spacing();
    const auto& stroke = GetActiveOrLastStroke();
    ImGui::Text("Captured Raw Samples: %zu   |   Total Ingested: %llu   |   Dropped: %llu",
                stroke.size(),
                static_cast<unsigned long long>(m_totalIngestedPackets),
                static_cast<unsigned long long>(m_totalDroppedPackets));

    if (ImGui::Button("Reset Ingestion Counters")) {
        m_totalIngestedPackets = 0;
        m_totalDroppedPackets = 0;
        changed = true;
    }
    ImGui::SameLine();
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
