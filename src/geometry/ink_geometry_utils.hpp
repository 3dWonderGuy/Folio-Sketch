#pragma once
/**
 * =========================================================================================
 * @file ink_geometry_utils.hpp
 * @brief Geometric Primitives, Intersection Tests, and Defect Detection
 * =========================================================================================
 */

#include "folio_sketch/ink_stroke_data.hpp"
#include <vector>
#include <span>
#include <cstddef>

namespace Folio {
namespace InkInternal {

/**
 * @struct SegmentInfo
 * @brief Precomputed unit tangent and normal vectors for a stroke segment.
 */
struct SegmentInfo {
    double len = 0.0;
    double ux = 1.0;
    double uy = 0.0;
    double nx = 0.0;
    double ny = 1.0;
};

/**
 * @brief Precomputes unit tangents and normals for all consecutive segments in cleanPts.
 */
std::vector<SegmentInfo> ComputeSegments(std::span<const StrokePoint> cleanPts);

/**
 * @brief Fast 2D segment-segment intersection test with AABB bounding box pre-rejection.
 * @param inclusive If true, tests [0, 1] including endpoints (for loop pruning).
 *                  If false, tests interior (0, 1) excluding adjacent endpoints (for defect testing).
 */
bool IntersectSegments(const Point2D& p1, const Point2D& p2,
                       const Point2D& p3, const Point2D& p4,
                       Point2D& outHit,
                       bool inclusive = false);

/**
 * @brief Prunes local swallowtail self-intersection loops caused by turn radius R < stroke radius r.
 */
void PruneSwallowtailLoops(std::vector<Point2D>& rail, size_t maxLoopSpan = 40);

/**
 * @brief Exhaustively detects self-intersections along ribbon boundaries for QA and debugging.
 */
void DetectSelfIntersections(const std::vector<Point2D>& leftRail,
                             const std::vector<Point2D>& rightRail,
                             bool isClosedPolygon,
                             std::vector<Point2D>& outIntersections);

/**
 * @brief Identifies vertices that step backward against all local stroke segment tangents.
 */
std::vector<size_t> DetectRetrogradeSteps(const std::vector<Point2D>& sidePts,
                                         std::span<const SegmentInfo> segs,
                                         bool isRight);

} // namespace InkInternal
} // namespace Folio
