#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_generator/ink_generator_internal.hpp
 * @brief Internal Nib Geometry, Support Offsets, and Cap Tracing Primitives
 * =========================================================================================
 */

#include "pipelines/vector/ink_generator.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {
namespace InkInternal {

/**
 * @brief Constructs the 2D polygon vertices defining the physical shape of a pen nib.
 */
std::vector<Point2D> GetNibVertices(CapType capType, double r);

/**
 * @brief Computes extreme boundary offsets (outLeft, outRight) for a given nib and segment normal.
 */
void GetRibbonOffsets(CapType capType, double r, double nx, double ny,
                      Point2D& outLeft, Point2D& outRight);

/**
 * @brief Traces a smooth rounded endcap arc between startPt and endPt bowing in direction (ux, uy).
 */
void TraceRoundCap(InkPath& path,
                   const StrokePoint& center,
                   const Point2D& startPt,
                   const Point2D& endPt,
                   double ux, double uy);

/**
 * @brief Traces oriented polygon endcap facets for Square and Chisel nibs.
 */
void TracePolygonCap(InkPath& path,
                     const StrokePoint& center,
                     const Point2D& startPt,
                     const Point2D& endPt,
                     double ux, double uy,
                     CapType capType);

/**
 * @brief Resolves tilt envelope offsets for discontinuous or pencil strokes.
 */
StrokePoint ResolveEnvelopePoint(const StrokePoint& pt, StrokePattern pattern) noexcept;

/**
 * @brief Accesses mutable debug info singleton for internal ribbon instrumentation.
 */
StrokeDebugInfo& GetMutableDebugInfo() noexcept;

} // namespace InkInternal
} // namespace Folio
