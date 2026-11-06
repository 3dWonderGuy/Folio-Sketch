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
#include <algorithm>
#include "folio_sketch/core/raw_input.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"

namespace Folio::Debug {

using RawInputSample = Folio::RawInputSample;
using CanvasTransform = Folio::Backends::CanvasTransform;

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
     * @brief High-frequency batch event handler. Supports synthetic sub-packet oversampling
     *        (1x, 2x, 4x, 8x) to stress-test 240Hz-480Hz digitizer burst ingestion on desktop.
     * @param event Incoming SDL_Event.
     * @param transform Active canvas transform.
     * @param outBatch Vector of ordered samples (contains 1 to K samples).
     * @param outEventType +1 for Down, 2 for Motion (while down), 3 for Up, 0 if hover/non-stroke.
     * @return True if raw sensor state changed.
     */
    bool HandleEventBatch(const SDL_Event& event,
                          const CanvasTransform& transform,
                          std::vector<RawInputSample>& outBatch,
                          int& outEventType);

    /**
     * @brief Records real-time physical tip lag metrics between raw hardware input and modeled output.
     */
    void UpdateTelemetryTipLag(float rawX, float rawY, float modX, float modY, float predX, float predY);

    /**
     * @brief Records CPU duration (in microseconds) consumed by the physics modeler for one batch.
     */
    void RecordModelerTimeUs(float timeUs);

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

    // Phase 1: High-Frequency Digitizer Burst & Telemetry Controls
    [[nodiscard]] int GetOversampleFactor() const noexcept { return m_oversampleFactor; }
    void SetOversampleFactor(int factor) noexcept { m_oversampleFactor = std::clamp(factor, 1, 8); }
    [[nodiscard]] float GetMicroJitterMm() const noexcept { return m_microJitterMm; }
    void SetMicroJitterMm(float jitterMm) noexcept { m_microJitterMm = std::max(0.0f, jitterMm); }
    [[nodiscard]] float GetPacketDropRate() const noexcept { return m_packetDropRate; }
    void SetPacketDropRate(float dropRate) noexcept { m_packetDropRate = std::clamp(dropRate, 0.0f, 0.5f); }

    [[nodiscard]] float GetLiveTipLagMm() const noexcept { return m_liveTipLagMm; }
    [[nodiscard]] float GetLivePredLagMm() const noexcept { return m_livePredLagMm; }
    [[nodiscard]] float GetLastModelerTimeUs() const noexcept { return m_lastModelerTimeUs; }
    [[nodiscard]] int GetLastBatchSize() const noexcept { return m_lastBatchSize; }
    [[nodiscard]] uint64_t GetTotalIngested() const noexcept { return m_totalIngestedPackets; }
    [[nodiscard]] uint64_t GetTotalDropped() const noexcept { return m_totalDroppedPackets; }

private:
    void RecordTimestamp(double nowSec);

    // Synthetic Batch Ingestion Settings
    int   m_oversampleFactor = 1;      ///< 1x = native, 2x = ~120Hz, 4x = ~240Hz, 8x = ~480Hz
    float m_microJitterMm    = 0.0f;   ///< Noise injected into synthetic sub-packets (mm)
    float m_packetDropRate   = 0.0f;   ///< Simulated hardware dropouts (0.0 to 0.3)
    bool  m_hasPrevSample    = false;
    RawInputSample m_prevSample{};

    // Telemetry & Benchmark Metrics
    int      m_lastBatchSize        = 1;
    float    m_lastModelerTimeUs    = 0.0f;
    float    m_liveTipLagMm         = 0.0f;
    float    m_livePredLagMm        = 0.0f;
    uint64_t m_totalIngestedPackets = 0;
    uint64_t m_totalDroppedPackets  = 0;

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
