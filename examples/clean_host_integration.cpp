/**
 * =========================================================================================
 * @file examples/clean_host_integration.cpp
 * @brief Standalone production host integration example for Folio-Sketch & Folio-Backends.
 * =========================================================================================
 *
 * Demonstrates the decoupled ~60-line CanvasInkingBridge architecture connecting raw
 * windowing/digitizer events directly to the Tier 1 math engine and Tier 2 GPU renderer,
 * with zero dependencies on diagnostic inspector UI or developer workbench tools.
 */

#include <cstdint>
#include <functional>
#include <span>
#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"

namespace HostApp {

/**
 * @class CanvasInkingBridge
 * @brief Turnkey coordinator managing live wet-stroke streaming and dry document handoff.
 */
class CanvasInkingBridge {
public:
    explicit CanvasInkingBridge(float dpi = 160.0f)
        : m_pxToMm(25.4f / dpi)
    {
        m_renderer.Init();
    }

    ~CanvasInkingBridge() {
        m_renderer.Shutdown();
    }

    void OnPointerDown(float xPx, float yPx, float pressure, uint64_t timestampNs,
                       const Folio::StrokeStyle& style) {
        m_active = true;
        m_uploadedCount = 0;

        Folio::RawInputSample sample{
            .x = xPx * m_pxToMm,
            .y = yPx * m_pxToMm,
            .pressure = pressure,
            .timeNs = static_cast<int64_t>(timestampNs)
        };
        m_engine.BeginStroke(sample, style);
    }

    void OnPointerMotion(float xPx, float yPx, float pressure, uint64_t timestampNs,
                         int vpWidth, int vpHeight,
                         const Folio::Backends::CanvasTransform& transform) {
        if (!m_active) return;

        Folio::RawInputSample sample{
            .x = xPx * m_pxToMm,
            .y = yPx * m_pxToMm,
            .pressure = pressure,
            .timeNs = static_cast<int64_t>(timestampNs)
        };
        m_engine.AppendPoint(sample.x, sample.y, sample.pressure, sample.TimeSec());

        std::span<const Folio::CapsuleInstance> live = m_engine.LiveCapsuleSpan();
        if (live.size() > m_uploadedCount) {
            std::span<const Folio::CapsuleInstance> delta = live.subspan(m_uploadedCount);
            m_renderer.AppendInstances(delta, m_uploadedCount);
            m_uploadedCount = live.size();

            // Render live incremental wet stroke
            m_renderer.Render(transform, vpWidth, vpHeight, {}, /*singlePassWet=*/true);
        }
    }

    void OnPointerUp(const std::function<void(const Folio::FinishedStrokeData&)>& onCommitDry) {
        if (!m_active) return;
        m_active = false;

        Folio::FinishedStrokeData finished = m_engine.FinishStroke();
        if (onCommitDry) {
            onCommitDry(finished); // Bake stroke into background document canvas
        }
        m_uploadedCount = 0;
    }

    [[nodiscard]] bool IsInking() const noexcept { return m_active; }

private:
    float m_pxToMm{25.4f / 160.0f};
    bool m_active{false};
    size_t m_uploadedCount{0};
    Folio::InkEngine m_engine;
    Folio::Backends::GlCapsuleRenderer m_renderer;
};

} // namespace HostApp
