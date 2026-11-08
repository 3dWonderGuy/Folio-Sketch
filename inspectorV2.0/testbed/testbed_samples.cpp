/**
 * =========================================================================================
 * @file inspectorV2.0/testbed/testbed_samples.cpp
 * @brief Synthetic Scribble & Repro114 Defect Ingestion
 * =========================================================================================
 */

#include "inspectorV2.0/testbed/testbed_samples.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include <cmath>
#include <vector>

namespace Folio::V2 {

void InjectTestScribble(LayerManager& layerManager) {
    auto& samples = layerManager.GetMutableRawSamples();
    samples.clear();
    for (int i = 0; i < 260; ++i) {
        const float t = static_cast<float>(i) * 0.045f;
        Folio::RawInputSample s{};
        s.x = 520.0f + std::cos(t) * (140.0f + 18.0f * t) * 0.55f + std::sin(t * 2.3f) * 60.0f;
        s.y = 420.0f + std::sin(t * 1.15f) * 150.0f;
        s.pressure = 0.35f + 0.5f * (0.5f + 0.5f * std::sin(t * 0.8f));
        s.tiltX = 0.0f;
        s.tiltY = 0.0f;
        s.timestampSec = static_cast<double>(i) * 0.008;
        s.timeNs = static_cast<int64_t>(s.timestampSec * 1e9);
        samples.push_back(s);
    }
    layerManager.ReplayRawStroke();
}

void LoadRepro114(LayerManager& layerManager) {
    static const struct {
        double x, y;
        float r, p, tilt, pad;
    } kRepro114Points[] = {
#include "../../inspector/repro114.inc"
    };

    auto& samples = layerManager.GetMutableRawSamples();
    samples.clear();
    samples.reserve(sizeof(kRepro114Points) / sizeof(kRepro114Points[0]));
    double t = 0.0;
    for (const auto& pt : kRepro114Points) {
        Folio::RawInputSample s{};
        s.x = pt.x;
        s.y = pt.y;
        s.pressure = (pt.p > 0.001f) ? pt.p : 1.0f;
        s.tiltX = 0.0f;
        s.tiltY = 0.0f;
        s.tiltRad = pt.tilt;
        s.timestampSec = t;
        s.timeNs = static_cast<int64_t>(t * 1e9);
        samples.push_back(s);
        t += 1.0 / 240.0;
    }
    layerManager.ReplayRawStroke();
}

} // namespace Folio::V2
