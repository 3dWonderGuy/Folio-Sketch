#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/engine/stroke_post_processor.hpp
 * @brief Post-Processing and Curve Simplification for Vector Ink
 * =========================================================================================
 *
 * Responsibilities:
 * - Curve Simplification: Decimates redundant collinear vertices via a shape-preserving
 *   Ramer-Douglas-Peucker (RDP) algorithm, retaining sharp corners and dynamic thickness.
 * - Radius Smoothing: Spatial Gaussian filter over stroke thickness.
 *
 * Implementations live in stroke_post_processor.cpp.
 */

#include <vector>
#include "core/ink_stroke_data.hpp"

namespace Folio {

class StrokePostProcessor {
public:
    /**
     * @brief Simplifies stroke centerline points by decimating redundant collinear vertices.
     *
     * Shape-preserving Ramer-Douglas-Peucker: a span [A, B] is collapsed into a single chord only if
     * every interior point satisfies ALL of:
     *  - perpendicular distance to the chord <= toleranceMm
     *  - direction deviation of its segment  <= maxTurnDeg (prevents faceted peaks on arcs)
     *  - radius deviation from linear interp <= radiusTolerance * r (preserves thickness profile)
     *
     * @param points Input centerline vertices.
     * @param toleranceMm Distance tolerance in millimeters.
     * @param maxTurnDeg Maximum angular deviation (degrees) between any dropped segment and the chord.
     * @param radiusTolerance Maximum relative radius error for dropped points.
     * @return Decimated vertex vector.
     */
    [[nodiscard]] static std::vector<StrokePoint> Simplify(
        const std::vector<StrokePoint>& points,
        float toleranceMm = 0.015f,
        float maxTurnDeg = 4.0f,
        float radiusTolerance = 0.08f);

    /**
     * @brief Smooths the radius profile of a stroke along its arc-length using a spatial Gaussian filter.
     *
     * Converts sharp, jagged thickness peaks and sawtooth spikes into soft, rounded hills.
     * Operates in true physical millimeter arc-length space so smoothing is invariant to point density.
     * Uses per-thread scratch buffers, so repeated calls do not allocate once warmed up.
     *
     * @param points Input/output stroke points whose .r radii are smoothed in-place.
     * @param sigmaMm Standard deviation of the Gaussian kernel in millimeters (e.g. 2.0 - 5.0 mm).
     * @param passes Number of smoothing passes (e.g. 1 or 2).
     */
    static void SmoothRadiiGaussian(std::vector<StrokePoint>& points,
                                    float sigmaMm = 3.5f,
                                    int passes = 2) noexcept;
};

} // namespace Folio
