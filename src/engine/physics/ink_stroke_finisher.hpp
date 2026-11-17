#pragma once
/**
 * =========================================================================================
 * @file src/engine/ink_stroke_finisher.hpp
 * @brief Stroke Finalization, Edge-Case Handling, and Collision Packaging
 * =========================================================================================
 *
 * Implements the completion stage of a stroke gesture:
 * 1. Single-tap / click edge-case handling (synthesizing a circular dot if stationary).
 * 2. Optional flick tail-tapering (smoothly tapering stroke release, importnat on high latency
 *   stylus devices to avoid abrupt stroke termination).
 * 3. Swept segment generation for distance hit-testing and point-based eraser tools.
 */

#include <vector>
#include "core/ink_stroke_data.hpp"
#include "core/ink_config.hpp"

namespace Folio {

class InkStrokeFinisher {
public:

    /**
     * @brief Ensures a tap/click produces a visible dot if the tremor filter swallowed lateral motion.
     */
    static void ResolveSingleTapEdgeCase(
        const std::vector<StylusInputPoint>& rawInputs,
        float tapWidth,
        std::vector<StrokePoint>& inoutModeledPoints);

    /**
     * @brief Applies smooth geometric tapering to the tail of the stroke upon pointer lift.
     */
    static void ApplyTailTaper(
        const InkingConfig& config,
        const StrokeStyle& style,
        std::vector<StrokePoint>& inoutModeledPoints);

    /**
     * @brief Builds linear 1D collision capsules across all consecutive modeled points.
     */
    [[nodiscard]] static std::vector<Segment1D> BuildCollisionSegments(
        const std::vector<StrokePoint>& modeledPoints);
};

} // namespace Folio
