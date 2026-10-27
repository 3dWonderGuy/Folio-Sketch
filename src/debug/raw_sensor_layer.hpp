#pragma once
/**
 * =========================================================================================
 * @file src/debug/raw_sensor_layer.hpp
 * @brief Step 1: Raw Digitizer Sensor HUD, Rolling Polling Rate Telemetry, and .inc Dumper
 * =========================================================================================
 */

#include <SDL3/SDL.h>
#include <imgui.h>
#include <vector>
#include <deque>
#include <string>

namespace Folio::Debug {

/**
 * @struct RawInputSample
 * @brief Raw hardware digitizer or mouse telemetry sample captured prior to physics modeling.
 */
struct RawInputSample {
    float x, y;
    float pressure;
    float tiltRad;
    double timestampSec;
};

/**
 * @struct CanvasTransform
 * @brief 2D view transform mapping world/canvas coordinates to screen pixels.
 */
struct CanvasTransform {
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;

    [[nodiscard]] ImVec2 WorldToScreen(float wx, float wy) const noexcept {
        return ImVec2(wx * zoom + panX, wy * zoom + panY);
    }

    [[nodiscard]] ImVec2 ScreenToWorld(float sx, float sy) const noexcept {
        float invZ = (zoom > 1e-5f) ? (1.0f / zoom) : 1.0f;
        return ImVec2((sx - panX) * invZ, (sy - panY) * invZ);
    }
};

/**
 * @class RawSensorHUD
 * @brief Captures raw SDL3 stylus/mouse events, computes real-time polling rate (Hz) and dt (ms),
 *        renders a corner telemetry HUD + raw hardware crosshair overlay, and dumps strokes to .inc.
 */
class RawSensorHUD {
public:
    RawSensorHUD() = default;

    /**
     * @brief Processes an incoming SDL3 event and records raw sample telemetry if applicable.
     * @param event Incoming SDL_Event.
     * @param transform Active canvas transform (converts screen coords to canvas world coords).
     * @param outSample Populated if this event generated a stroke Down/Move/Up sample.
     * @param outEventType +1 for Down, 2 for Motion (while down), 3 for Up, 0 if hover/non-stroke.
     * @return True if raw sensor state changed (marks render loop dirty).
     */
    bool HandleEvent(const SDL_Event& event,
                     const CanvasTransform& transform,
                     RawInputSample& outSample,
                     int& outEventType);

    /**
     * @brief Renders the corner HUD overlay and (if enabled) the raw hardware input crosshairs & polylines.
     */
    void Render(ImDrawList* drawList);
    void Render(ImDrawList* drawList, const CanvasTransform& transform);

    /**
     * @brief Renders ImGui controls for toggling raw hardware crosshair overlay and dumping to .inc.
     * @param baseRadius Nominal stroke radius used when serializing to repro114.inc format.
     * @return True if any UI control was interacted with.
     */
    bool RenderControls(float baseRadius = 12.5f);

    /**
     * @brief Serializes the active or last stroke's std::vector<RawInputSample> into C++ array format
     *        matching test/repro114.inc (`{ x, y, r, pressure, tiltX, tiltY },`).
     */
    bool DumpStrokeToInc(const std::string& filePath = "dumped_stroke.inc", float defaultRadius = 12.5f);

    // Accessors
    [[nodiscard]] const std::vector<RawInputSample>& GetActiveOrLastStroke() const noexcept {
        return m_activeSamples.empty() ? m_lastStrokeSamples : m_activeSamples;
    }
    void SetStrokeSamples(const std::vector<RawInputSample>& samples) {
        m_lastStrokeSamples = samples;
        m_activeSamples.clear();
    }
    void ClearSamples() {
        m_activeSamples.clear();
        m_lastStrokeSamples.clear();
    }

    [[nodiscard]] bool IsShowCrosshairs() const noexcept { return m_showRawCrosshairs; }
    void SetShowCrosshairs(bool show) noexcept { m_showRawCrosshairs = show; }

    [[nodiscard]] float GetCurrentX() const noexcept { return m_currentX; }
    [[nodiscard]] float GetCurrentY() const noexcept { return m_currentY; }
    [[nodiscard]] float GetCurrentPressure() const noexcept { return m_currentPressure; }
    [[nodiscard]] float GetCurrentTiltRad() const noexcept { return m_currentTiltRad; }
    [[nodiscard]] float GetCurrentTiltXRad() const noexcept { return m_tiltXRad; }
    [[nodiscard]] float GetCurrentTiltYRad() const noexcept { return m_tiltYRad; }
    [[nodiscard]] float GetPollingRateHz() const noexcept { return m_pollingRateHz; }
    [[nodiscard]] float GetDeltaTimeMs() const noexcept { return m_deltaTimeMs; }
    [[nodiscard]] bool IsPenDown() const noexcept { return m_penDown || m_mouseDown; }

private:
    void RecordTimestamp(double nowSec);

    float m_currentX = 0.0f;
    float m_currentY = 0.0f;
    float m_currentPressure = 0.0f;
    float m_tiltXRad = 0.0f;
    float m_tiltYRad = 0.0f;
    float m_currentTiltRad = 0.0f;
    double m_lastTimestampSec = 0.0;
    float m_deltaTimeMs = 0.0f;
    float m_pollingRateHz = 0.0f;

    bool m_penDown = false;
    bool m_mouseDown = false;
    bool m_showRawCrosshairs = true;
    bool m_showHudOverlay = true;

    std::deque<double> m_rollingTimestampsSec;
    std::vector<RawInputSample> m_activeSamples;
    std::vector<RawInputSample> m_lastStrokeSamples;
    std::string m_lastDumpStatus;
};

} // namespace Folio::Debug
