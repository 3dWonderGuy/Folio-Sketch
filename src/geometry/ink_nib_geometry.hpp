#pragma once
/**
 * =========================================================================================
 * @file ink_nib_geometry.hpp
 * @brief Stylus Nib Profiles, Oriented Vertices, and Ribbon Support Offsets
 * =========================================================================================
 */

#include "folio_sketch/ink_stroke_data.hpp"
#include <vector>

namespace Folio {
namespace InkInternal {

/**
 * @brief Returns the oriented polygon vertices for a given cap type centered at origin (0, 0).
 *        Round: 32-step circle; Square: 3:1 aspect highlighter blade; Chisel: 45° calligraphy blade.
 */
std::vector<Point2D> GetNibVertices(CapType capType, double r);

/**
 * @brief Computes extreme boundary offsets (outLeft, outRight) for a given nib and segment normal.
 */
void GetRibbonOffsets(CapType capType, double r, double nx, double ny,
                      Point2D& outLeft, Point2D& outRight);

} // namespace InkInternal
} // namespace Folio
