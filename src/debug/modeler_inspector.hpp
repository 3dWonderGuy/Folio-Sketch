#pragma once
/**
 * =========================================================================================
 * @file src/debug/modeler_inspector.hpp
 * @brief Step 2: Google Stroke Modeler Live Physics Tuning & Visual Overlays
 * =========================================================================================
 */

#include <imgui.h>
#include <span>
#include <vector>
#include "folio_sketch/ink_config.hpp"
#include "folio_sketch/ink_stroke_data.hpp"
#include "debug/raw_sensor_layer.hpp"

namespace Folio::Debug {

/**
 * @class ModelerInspector
 * @brief Exposes Google Ink Stroke Modeler parameters via ImGui controls and renders
 *        raw input dots, modeled centerline polylines, and Kalman prediction trails.
 */
class ModelerInspector {
public:
    ModelerInspector() = default;

    /**
     * @brief Renders ImGui controls wired directly to InkingConfig physics parameters.
     * @param config Active InkingConfig reference.
     * @return True if any physics parameter or overlay toggle changed.
     */
    bool RenderUI(InkingConfig& config);

    /**
     * @brief Renders visual overlays on the canvas draw list.
     * @param drawList Target ImDrawList.
     * @param rawSamples Raw hardware input samples.
     * @param modeledPoints Confirmed Google Stroke Modeler output centerline points.
     * @param predictedPoints Extrapolated Kalman prediction lookahead points.
     * @param transform Active canvas coordinate transform.
     */
    void RenderOverlays(ImDrawList* drawList,
                        std::span<const RawInputSample> rawSamples,
                        std::span<const StrokePoint> modeledPoints,
                        std::span<const StrokePoint> predictedPoints,
                        const CanvasTransform& transform) const;

    [[nodiscard]] bool ShowRawInputDots() const noexcept { return m_showRawPoints; }
    [[nodiscard]] bool ShowModeledCenterline() const noexcept { return m_showModeledCenterline; }
    [[nodiscard]] bool ShowKalmanPrediction() const noexcept { return m_showKalmanSpine; }

private:
    bool m_showRawPoints = true;
    bool m_showModeledCenterline = true;
    bool m_showKalmanSpine = true;
};

} // namespace Folio::Debug
