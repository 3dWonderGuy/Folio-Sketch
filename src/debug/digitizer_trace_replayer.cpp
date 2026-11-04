/**
 * =========================================================================================
 * @file src/debug/digitizer_trace_replayer.cpp
 * @brief High-Frequency Digitizer Batch & Trace Replayer for Modeler & Latency Benchmarking
 * =========================================================================================
 */

#include "debug/digitizer_trace_replayer.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio::Debug {

std::vector<RawInputSample> DigitizerTraceReplayer::GenerateTrace(
    DigitizerTraceType type,
    float rateHz,
    float originX,
    float originY,
    float scale,
    float speedMmPerSec)
{
    rateHz = std::clamp(rateHz, 30.0f, 1000.0f);
    speedMmPerSec = std::clamp(speedMmPerSec, 100.0f, 5000.0f);
    scale = std::max(0.1f, scale);

    std::vector<RawInputSample> trace;

    if (type == DigitizerTraceType::CursiveLoop) {
        // Continuous figure-8 lemniscate with alternating tight curvature loops
        const float a = 80.0f * scale; // Width amplitude
        const float b = 40.0f * scale; // Height amplitude
        // Approximate perimeter for lemniscate ~ 5.24 * a
        const float perimeterMm = 5.24f * a;
        const float durationSec = perimeterMm / speedMmPerSec;
        const int numSamples = std::max(10, static_cast<int>(std::ceil(durationSec * rateHz)));
        const double dt = durationSec / static_cast<double>(numSamples);

        trace.reserve(numSamples + 1);
        for (int i = 0; i <= numSamples; ++i) {
            double tNorm = static_cast<double>(i) / static_cast<double>(numSamples);
            double theta = tNorm * 2.0 * M_PI;
            double timeSec = static_cast<double>(i) * dt;

            // Lemniscate of Gerono: x = a * cos(theta), y = b * sin(theta) * cos(theta)
            float x = originX + static_cast<float>(a * std::sin(theta));
            float y = originY + static_cast<float>(b * std::sin(2.0 * theta));

            // Dynamic pressure modulated with speed and curvature (0.4 to 0.8)
            float pressure = static_cast<float>(0.55 + 0.25 * std::cos(theta * 2.0));

            trace.push_back(RawInputSample{
                .x = x,
                .y = y,
                .pressure = std::clamp(pressure, 0.1f, 1.0f),
                .tiltRad = 0.0f,
                .timestampSec = timeSec
            });
        }
    }
    else if (type == DigitizerTraceType::ZigZag90) {
        // 5 alternating 90-degree corners with segment length 35mm
        const float segLenMm = 35.0f * scale;
        constexpr int kNumCorners = 5;
        const float totalDistanceMm = segLenMm * (kNumCorners + 1);
        const float durationSec = totalDistanceMm / speedMmPerSec;
        const int numSamples = std::max(10, static_cast<int>(std::ceil(durationSec * rateHz)));
        const double dt = durationSec / static_cast<double>(numSamples);

        trace.reserve(numSamples + 1);
        float curX = originX;
        float curY = originY;

        // Waypoints for alternating 90-degree zig-zag
        std::vector<ImVec2> waypoints;
        waypoints.push_back(ImVec2(originX, originY));
        for (int c = 0; c <= kNumCorners; ++c) {
            if (c % 2 == 0) {
                curX += segLenMm;
            } else {
                curY += (c % 4 == 1) ? segLenMm : -segLenMm;
            }
            waypoints.push_back(ImVec2(curX, curY));
        }

        // Interpolate along waypoints with constant speed
        for (int i = 0; i <= numSamples; ++i) {
            double tNorm = static_cast<double>(i) / static_cast<double>(numSamples);
            double currentDist = tNorm * totalDistanceMm;
            double timeSec = static_cast<double>(i) * dt;

            // Find current segment
            float accumulated = 0.0f;
            float px = waypoints.back().x;
            float py = waypoints.back().y;
            for (size_t s = 0; s + 1 < waypoints.size(); ++s) {
                if (currentDist <= accumulated + segLenMm || s + 2 == waypoints.size()) {
                    float localT = static_cast<float>((currentDist - accumulated) / segLenMm);
                    localT = std::clamp(localT, 0.0f, 1.0f);
                    px = waypoints[s].x + localT * (waypoints[s + 1].x - waypoints[s].x);
                    py = waypoints[s].y + localT * (waypoints[s + 1].y - waypoints[s].y);
                    break;
                }
                accumulated += segLenMm;
            }

            trace.push_back(RawInputSample{
                .x = px,
                .y = py,
                .pressure = 0.65f,
                .tiltRad = 0.0f,
                .timestampSec = timeSec
            });
        }
    }
    else { // TipLagBenchmark: Straight high-speed line
        const float lengthMm = 180.0f * scale;
        const float durationSec = lengthMm / speedMmPerSec;
        const int numSamples = std::max(10, static_cast<int>(std::ceil(durationSec * rateHz)));
        const double dt = durationSec / static_cast<double>(numSamples);

        trace.reserve(numSamples + 1);
        for (int i = 0; i <= numSamples; ++i) {
            double tNorm = static_cast<double>(i) / static_cast<double>(numSamples);
            double timeSec = static_cast<double>(i) * dt;

            trace.push_back(RawInputSample{
                .x = originX + static_cast<float>(tNorm * lengthMm),
                .y = originY,
                .pressure = 0.7f,
                .tiltRad = 0.0f,
                .timestampSec = timeSec
            });
        }
    }

    return trace;
}

void DigitizerTraceReplayer::StartReplay(DigitizerTraceType type, float rateHz, float speed, bool loop,
                                         float originX, float originY) {
    m_selectedType = type;
    m_targetRateHz = rateHz;
    m_replaySpeed = std::max(0.05f, speed);
    m_loopReplay = loop;

    m_activeReplayTrace = GenerateTrace(type, rateHz, originX, originY, 1.0f, m_benchmarkSpeedMmPerSec);
    m_currentReplayIndex = 0;
    m_isPlaying = !m_activeReplayTrace.empty();
    m_replayStartTimeSec = 0.0;
    m_lastTickTimeSec = 0.0;
}

void DigitizerTraceReplayer::StopReplay(Folio::InkEngine* engine) {
    if (m_isPlaying && engine && engine->IsStrokeActive()) {
        engine->FinishStroke();
    }
    m_isPlaying = false;
    m_currentReplayIndex = 0;
}

bool DigitizerTraceReplayer::Tick(double nowSec, Folio::InkEngine& engine, RawSensorHUD& hud,
                                  const StrokeStyle& style, float zoom,
                                  const std::function<void(FinishedStrokeData&&)>& onStrokeFinished) {
    if (!m_isPlaying || m_activeReplayTrace.empty()) return false;

    if (m_replayStartTimeSec <= 0.0) {
        m_replayStartTimeSec = nowSec;
        m_lastTickTimeSec = nowSec;

        // Ingest the initial contact point
        const auto& p0 = m_activeReplayTrace[0];
        engine.BeginStroke(p0.x, p0.y, p0.pressure, nowSec, style, zoom, 0.0f, 0.0f);
        m_currentReplayIndex = 1;
        return true;
    }

    double elapsedReplaySec = (nowSec - m_replayStartTimeSec) * m_replaySpeed;
    bool ingestedAny = false;

    while (m_currentReplayIndex < m_activeReplayTrace.size()) {
        const auto& pt = m_activeReplayTrace[m_currentReplayIndex];
        if (pt.timestampSec > elapsedReplaySec) {
            break; // Not yet due for this frame
        }

        // Synthetic packet drop simulation
        if (m_syntheticDropRate > 0.001f) {
            float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r < m_syntheticDropRate) {
                ++m_currentReplayIndex;
                continue;
            }
        }

        double ptWallTimeSec = m_replayStartTimeSec + pt.timestampSec / m_replaySpeed;
        engine.AppendPoint(pt.x, pt.y, pt.pressure, ptWallTimeSec, 0.0f, 0.0f);
        ingestedAny = true;
        ++m_currentReplayIndex;
    }

    // Check if stroke completed
    if (m_currentReplayIndex >= m_activeReplayTrace.size()) {
        if (engine.IsStrokeActive()) {
            FinishedStrokeData fin = engine.FinishStroke();
            hud.SetStrokeSamples(m_activeReplayTrace);
            if (onStrokeFinished && !fin.modeledPoints.empty()) {
                onStrokeFinished(std::move(fin));
            }
        }
        if (m_loopReplay) {
            m_replayStartTimeSec = nowSec;
            m_currentReplayIndex = 0;
            const auto& p0 = m_activeReplayTrace[0];
            engine.BeginStroke(p0.x, p0.y, p0.pressure, nowSec, style, zoom, 0.0f, 0.0f);
            m_currentReplayIndex = 1;
        } else {
            m_isPlaying = false;
        }
    }

    return ingestedAny;
}

BenchmarkMetrics DigitizerTraceReplayer::InjectTraceImmediately(
    DigitizerTraceType type,
    float rateHz,
    Folio::InkEngine& engine,
    RawSensorHUD& hud,
    const StrokeStyle& style,
    float zoom,
    float originX,
    float originY,
    float speedMmPerSec,
    float packetDropRate,
    const std::function<void(FinishedStrokeData&&)>& onStrokeFinished)
{
    BenchmarkMetrics metrics{};
    metrics.simulatedRateHz = rateHz;

    auto trace = GenerateTrace(type, rateHz, originX, originY, 1.0f, speedMmPerSec);
    if (trace.empty()) return metrics;

    metrics.totalPackets = static_cast<int>(trace.size());
    metrics.traceDurationSec = static_cast<float>(trace.back().timestampSec);

    auto startOverall = std::chrono::high_resolution_clock::now();

    // 1. Begin Stroke
    const auto& p0 = trace[0];
    engine.BeginStroke(p0.x, p0.y, p0.pressure, 0.0, style, zoom, 0.0f, 0.0f);

    float sumLagMm = 0.0f;
    int lagSamples = 0;
    float maxCpuUs = 0.0f;
    float sumCpuUs = 0.0f;

    for (size_t i = 1; i < trace.size(); ++i) {
        const auto& pt = trace[i];

        // Simulate dropped packet
        if (packetDropRate > 0.001f) {
            float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r < packetDropRate) {
                metrics.droppedPackets++;
                continue;
            }
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.AppendPoint(pt.x, pt.y, pt.pressure, pt.timestampSec, 0.0f, 0.0f);
        auto t1 = std::chrono::high_resolution_clock::now();

        float dtUs = std::chrono::duration<float, std::micro>(t1 - t0).count();
        maxCpuUs = std::max(maxCpuUs, dtUs);
        sumCpuUs += dtUs;

        // Measure tip lag
        const auto& modeled = engine.GetLiveModeledPoints();
        if (!modeled.empty()) {
            const auto& modTip = modeled.back();
            float lag = std::hypot(pt.x - modTip.x, pt.y - modTip.y);
            metrics.maxTipLagMm = std::max(metrics.maxTipLagMm, lag);
            sumLagMm += lag;
            lagSamples++;
        }

        const auto& pred = engine.GetPredictedPoints();
        if (!pred.empty() && !modeled.empty()) {
            const auto& predTip = pred.back();
            const auto& modTip = modeled.back();
            float fwd = std::hypot(predTip.x - modTip.x, predTip.y - modTip.y);
            metrics.maxKalmanForwardMm = std::max(metrics.maxKalmanForwardMm, fwd);
        }
    }

    FinishedStrokeData fin = engine.FinishStroke();
    hud.SetStrokeSamples(trace);
    if (onStrokeFinished && !fin.modeledPoints.empty()) {
        onStrokeFinished(std::move(fin));
    }

    auto endOverall = std::chrono::high_resolution_clock::now();
    metrics.totalCpuTimeUs = std::chrono::duration<float, std::micro>(endOverall - startOverall).count();

    int processedCount = metrics.totalPackets - metrics.droppedPackets;
    if (processedCount > 0) {
        metrics.avgCpuTimePerPacketUs = sumCpuUs / static_cast<float>(processedCount);
        metrics.maxCpuTimePerPacketUs = maxCpuUs;
    }
    if (lagSamples > 0) {
        metrics.avgTipLagMm = sumLagMm / static_cast<float>(lagSamples);
    }
    metrics.valid = true;
    m_lastBenchmark = metrics;

    return metrics;
}

bool DigitizerTraceReplayer::RenderUI(Folio::InkEngine& engine, RawSensorHUD& hud,
                                      const StrokeStyle& style, const CanvasTransform& transform,
                                      const std::function<void(FinishedStrokeData&&)>& onStrokeFinished) {
    bool changed = false;

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Deterministic Trace Replayer & Benchmarking");
    ImGui::TextWrapped("Generate and inject high-rate synthetic digitizer streams to verify 240Hz-480Hz modeler stability.");

    const char* traceNames[] = {
        "Cursive Loop (Lemniscate 1500 px/s)",
        "Sharp 90-Deg Zig-Zag (1200 mm/s)",
        "Straight Tip-Lag Benchmark (1000 mm/s)"
    };
    int currentType = static_cast<int>(m_selectedType);
    if (ImGui::Combo("Geometric Pattern", &currentType, traceNames, IM_ARRAYSIZE(traceNames))) {
        m_selectedType = static_cast<DigitizerTraceType>(currentType);
        changed = true;
    }

    // Rate selection presets
    ImGui::Text("Simulated Hardware Polling Rate:");
    ImGui::SameLine();
    if (ImGui::RadioButton("60Hz", m_targetRateHz == 60.0f))   { m_targetRateHz = 60.0f; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("120Hz", m_targetRateHz == 120.0f)) { m_targetRateHz = 120.0f; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("240Hz", m_targetRateHz == 240.0f)) { m_targetRateHz = 240.0f; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("480Hz", m_targetRateHz == 480.0f)) { m_targetRateHz = 480.0f; changed = true; }

    changed |= ImGui::SliderFloat("Speed (mm/s)", &m_benchmarkSpeedMmPerSec, 200.0f, 3000.0f, "%.0f mm/s");
    changed |= ImGui::SliderFloat("Simulated Packet Drop Rate", &m_syntheticDropRate, 0.0f, 0.30f, "%.1f %%");

    ImGui::Spacing();

    // Replay buttons
    if (!m_isPlaying) {
        if (ImGui::Button("Run Real-Time Replay", ImVec2(180, 26))) {
            StartReplay(m_selectedType, m_targetRateHz, m_replaySpeed, m_loopReplay, 150.0f, 200.0f);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &m_loopReplay);
    } else {
        if (ImGui::Button("Stop Replay", ImVec2(180, 26))) {
            StopReplay(&engine);
            changed = true;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Run Instant Benchmark", ImVec2(180, 26))) {
        InjectTraceImmediately(m_selectedType, m_targetRateHz, engine, hud, style, transform.zoom,
                               150.0f, 200.0f, m_benchmarkSpeedMmPerSec, m_syntheticDropRate, onStrokeFinished);
        changed = true;
    }

    // Benchmark Results Display
    if (m_lastBenchmark.valid) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Last Benchmark Telemetry (%.0f Hz):",
                           m_lastBenchmark.simulatedRateHz);
        ImGui::BulletText("Total Packets: %d (Dropped: %d, Retention: %.1f%%)",
                          m_lastBenchmark.totalPackets,
                          m_lastBenchmark.droppedPackets,
                          100.0f * (1.0f - static_cast<float>(m_lastBenchmark.droppedPackets) /
                                           std::max(1, m_lastBenchmark.totalPackets)));
        ImGui::BulletText("Modeler Throughput: %.1f us/pkt (Max: %.1f us, Total: %.2f ms)",
                          m_lastBenchmark.avgCpuTimePerPacketUs,
                          m_lastBenchmark.maxCpuTimePerPacketUs,
                          m_lastBenchmark.totalCpuTimeUs * 1e-3f);
        ImGui::BulletText("Physical Tip Lag: Avg: %.2f mm  |  Max: %.2f mm (%.1f screen px)",
                          m_lastBenchmark.avgTipLagMm,
                          m_lastBenchmark.maxTipLagMm,
                          m_lastBenchmark.maxTipLagMm * transform.zoom);
        ImGui::BulletText("Kalman Forward Prediction: %.2f mm",
                          m_lastBenchmark.maxKalmanForwardMm);
    }

    return changed;
}

} // namespace Folio::Debug
