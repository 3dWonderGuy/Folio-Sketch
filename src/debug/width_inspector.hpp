#pragma once
/**
 * =========================================================================================
 * @file src/debug/width_inspector.hpp
 * @brief Step 3: Width, Pressure & Velocity Evaluator Panel and Radius Curve Plotter
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
 * @class WidthInspector
 * @brief Isolates dynamic stroke radius (r_i) evaluation from mesh geometry,
 *        provides live ImGui sliders, renders hollow radius discs, and plots
 *        evaluated radius vs. stroke arc-length via ImGui::PlotLines.
 */
class WidthInspector {
public:
    WidthInspector() = default;

    /**
     * @brief Renders ImGui controls for width, pressure, velocity, tilt, EMA smoothing,
     *        and the Width Curve Plot (radius vs. stroke arc-length).
     * @param config Active InkingConfig.
     * @param style Active StrokeStyle (baseWidth, usePressure, useVelocity, useTilt).
     * @param modeledPoints Current modeled stroke points for the PlotLines graph.
     * @return True if any width evaluation parameter changed.
     */
    bool RenderUI(InkingConfig& config,
                  StrokeStyle& style,
                  std::span<const StrokePoint> modeledPoints);

    /**
     * @brief Re-evaluates radius r_i across an existing sequence of StrokePoints
     *        using StrokeWidthEvaluator and current InkingConfig + StrokeStyle.
     */
    static void ReevaluateRadii(std::vector<StrokePoint>& points,
                                const InkingConfig& config,
                                const StrokeStyle& style,
                                float zoomScale = 1.0f);

    /**
     * @brief Renders hollow radius circles at each modeled point on the canvas.
     */
    void RenderOverlays(ImDrawList* drawList,
                        std::span<const StrokePoint> modeledPoints,
                        const CanvasTransform& transform) const;

    [[nodiscard]] bool ShowRadiusDiscs() const noexcept { return m_showRadiusDiscs; }
    void SetShowRadiusDiscs(bool show) noexcept { m_showRadiusDiscs = show; }

private:
    bool m_showRadiusDiscs = false;
    mutable std::vector<float> m_arcLengthSamples;
    mutable std::vector<float> m_arcLengthsMm;
};

} // namespace Folio::Debug
