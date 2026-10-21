#pragma once
/**
 * =========================================================================================
 * @file ink_stroke_filter.hpp
 * @brief Discrete Stroke Conditioning, Point Decimation, and Radius Regularization
 * =========================================================================================
 */

#include "folio_sketch/ink_stroke_data.hpp"
#include <vector>
#include <span>
#include <cstddef>

namespace Folio {
namespace InkInternal {

class StrokeFilter {
public:
    /**
     * @brief Pre-filters raw stroke points by eliminating submerged containment disks,
     *        enforcing radius-proportional minimum step separation, and pruning micro-jitter stubs.
     */
    static std::vector<StrokePoint> PreFilterPoints(std::span<const StrokePoint> pts);

    /**
     * @brief Regularizes radius rate-of-change across consecutive stroke points:
     *        Enforces Lipschitz continuity |dr/ds| <= maxSlope in bidirectional passes.
     */
    static void RegularizeRadiusSlope(std::vector<StrokePoint>& pts, double maxSlope = 0.85);

    /**
     * @brief Identifies sharp reversal hairpin turn points (turn angle > ~100°, cosTheta < cosThreshold).
     */
    static std::vector<size_t> FindHairpinTurns(std::span<const StrokePoint> pts, double cosThreshold = -0.20);
};

} // namespace InkInternal
} // namespace Folio
