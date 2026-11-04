#pragma once
/**
 * =========================================================================================
 * @file src/debug/digitizer_trace_replayer.hpp
 * @brief High-Frequency Digitizer Batch & Trace Replayer for Modeler & Latency Benchmarking
 * =========================================================================================
 *
 * Simulates high-rate digitizer hardware (60Hz, 120Hz, 240Hz, 480Hz) with microsecond
 * timestamps to stress-test the inking pipeline, measure physical tip lag, and test recovery
 * from dropped sub-frame packets.
 */

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <imgui.h>
#include "debug/raw_sensor_layer.hpp"
#include "folio_sketch/engine/ink_engine.hpp"

namespace Folio::Debug {

/**
 * @enum DigitizerTraceType
 * @brief Canonical high-velocity test trajectories for evaluating inking physics and latency.
 */
enum class DigitizerTraceType : int {
    CursiveLoop = 0,     ///< Bernoulli lemniscate / multi-lobe loops with alternating tight curvature (1500-2500 px/s)
    ZigZag90 = 1,        ///< Rapid 90-degree directional inflections testing corner overshoot vs clipping (1200 mm/s)
    TipLagBenchmark = 2  ///< Constant-speed straight stroke to accurately measure Euclidean tip-following distance
};

/**
 * @struct BenchmarkMetrics
 * @brief Statistics collected during a deterministic high-frequency trace run.
 */
struct BenchmarkMetrics {
    int totalPackets = 0;
    int droppedPackets = 0;
    float simulatedRateHz = 240.0f;
    float traceDurationSec = 0.0f;
    float totalCpuTimeUs = 0.0f;
    float avgCpuTimePerPacketUs = 0.0f;
    float maxCpuTimePerPacketUs = 0.0f;
    float maxTipLagMm = 0.0f;
    float avgTipLagMm = 0.0f;
    float maxKalmanForwardMm = 0.0f;
    bool valid = false;
};

/**
 * @class DigitizerTraceReplayer
 * @brief Generates and replays high-rate synthetic digitizer packet streams into InkEngine.
 */
class DigitizerTraceReplayer {
public:
    DigitizerTraceReplayer() = default;

    /**
     * @brief Generates a synthetic trajectory at a specified sampling rate.
     * @param type Desired geometric trace type.
     * @param rateHz Target digitizer frequency (e.g. 60, 120, 240, 480 Hz).
     * @param originX World X origin in mm.
     * @param originY World Y origin in mm.
     * @param scale Overall size multiplier.
     * @param speedMmPerSec Target velocity along the curve.
     * @return Ordered list of raw input samples with microsecond-accurate monotonic timestamps.
     */
    static std::vector<RawInputSample> GenerateTrace(
        DigitizerTraceType type,
        float rateHz = 240.0f,
        float originX = 150.0f,
        float originY = 200.0f,
        float scale = 1.0f,
        float speedMmPerSec = 1000.0f);

    /**
     * @brief Begins real-time frame-by-frame replay of a synthetic trace.
     */
    void StartReplay(DigitizerTraceType type, float rateHz, float speed = 1.0f, bool loop = false,
                     float originX = 150.0f, float originY = 200.0f);

    /**
     * @brief Halts active replay and finalizes the current stroke.
     */
    void StopReplay(Folio::InkEngine* engine = nullptr);

    [[nodiscard]] bool IsPlaying() const noexcept { return m_isPlaying; }

    /**
     * @brief Ticks the active real-time replay by pumping due packets into InkEngine.
     * @param onStrokeFinished Optional callback invoked when a stroke completes.
     * @return True if new packets were ingested during this tick.
     */
    bool Tick(double nowSec, Folio::InkEngine& engine, RawSensorHUD& hud,
              const StrokeStyle& style, float zoom,
              const std::function<void(FinishedStrokeData&&)>& onStrokeFinished = nullptr);

    /**
     * @brief Immediately injects an entire high-frequency trace synchronously to benchmark CPU throughput.
     */
    BenchmarkMetrics InjectTraceImmediately(
        DigitizerTraceType type,
        float rateHz,
        Folio::InkEngine& engine,
        RawSensorHUD& hud,
        const StrokeStyle& style,
        float zoom,
        float originX = 150.0f,
        float originY = 200.0f,
        float speedMmPerSec = 1000.0f,
        float packetDropRate = 0.0f,
        const std::function<void(FinishedStrokeData&&)>& onStrokeFinished = nullptr);

    /**
     * @brief Renders ImGui controls for synthetic trace generation and benchmark execution.
     * @return True if any setting or action was triggered.
     */
    bool RenderUI(Folio::InkEngine& engine, RawSensorHUD& hud, const StrokeStyle& style,
                  const CanvasTransform& transform,
                  const std::function<void(FinishedStrokeData&&)>& onStrokeFinished = nullptr);

    [[nodiscard]] const BenchmarkMetrics& GetLastBenchmark() const noexcept { return m_lastBenchmark; }

private:
    DigitizerTraceType m_selectedType = DigitizerTraceType::CursiveLoop;
    float m_targetRateHz = 240.0f;
    float m_replaySpeed = 1.0f;
    bool  m_loopReplay = false;
    float m_benchmarkSpeedMmPerSec = 1000.0f;
    float m_syntheticDropRate = 0.0f;

    bool m_isPlaying = false;
    std::vector<RawInputSample> m_activeReplayTrace;
    size_t m_currentReplayIndex = 0;
    double m_replayStartTimeSec = 0.0;
    double m_lastTickTimeSec = 0.0;

    BenchmarkMetrics m_lastBenchmark{};
};

} // namespace Folio::Debug
