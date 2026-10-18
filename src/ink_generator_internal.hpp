#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_generator/ink_generator_internal.hpp
 * @brief Internal Nib Geometry, Support Offsets, and Cap Tracing Primitives
 * =========================================================================================
 */

#include "folio_sketch/ink_generator.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {
namespace InkInternal {

/**
 * @brief Returns the oriented polygon vertices for a given cap type centered at origin (0, 0).
 */
std::vector<Point2D> GetNibVertices(CapType capType, double r);

/**
 * @brief Computes extreme boundary offsets (outLeft, outRight) for a given nib and normal.
 */
void GetRibbonOffsets(CapType capType, double r, double nx, double ny,
                      Point2D& outLeft, Point2D& outRight);

/**
 * @brief Traces a smooth rounded endcap arc between startPt and endPt bowing in direction (ux, uy).
 */
void TraceRoundCap(BLPath& path,
                   const StrokePoint& center,
                   const Point2D& startPt,
                   const Point2D& endPt,
                   double ux, double uy);

/**
 * @brief Traces oriented polygon endcap facets for Square and Chisel nibs.
 */
void TracePolygonCap(BLPath& path,
                     const StrokePoint& center,
                     const Point2D& startPt,
                     const Point2D& endPt,
                     double ux, double uy,
                     CapType capType);

/**
 * @brief Resolves tilt envelope offsets for discontinuous or pencil strokes.
 */
StrokePoint ResolveEnvelopePoint(const StrokePoint& pt, StrokePattern pattern) noexcept;

} // namespace InkInternal
} // namespace Folio
