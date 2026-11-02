#pragma once
/**
 * =========================================================================================
 * @file ink_ribbon_corner.hpp
 * @brief Ribbon Rail Corner Evaluation, Miter Clamping, and Arc Sweeping
 * =========================================================================================
 */

#include "folio_sketch/ink_stroke_data.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include <vector>

namespace Folio {
namespace InkInternal {

/**
 * @brief Appends pt to sidePts if Euclidean distance from previous vertex >= 1e-5 mm.
 */
void PushMonotonic(std::vector<Point2D>& sidePts, Point2D pt);

/**
 * @brief Pops overshooting interior corner vertices before appending the terminal cap.
 */
void PushTerminalCap(std::vector<Point2D>& sidePts, Point2D pt, double tX, double tY);

/**
 * @brief Processes an interior stroke corner, evaluating bisector miters, circular arcs,
 *        or polygonal nib facets, and appends boundary vertices to leftPts and rightPts.
 */
void ProcessCorner(const StrokePoint& pt,
                   CapType capType,
                   const SegmentInfo& sIn,
                   const SegmentInfo& sOut,
                   std::vector<Point2D>& leftPts,
                   std::vector<Point2D>& rightPts);

} // namespace InkInternal
} // namespace Folio
