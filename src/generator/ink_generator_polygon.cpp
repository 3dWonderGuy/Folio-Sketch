#include "folio_sketch/ink_generator.hpp"

#include <vector>
#include <cmath>

namespace Folio {

// =============================================================================
// 1. TOP-LEVEL DISPATCHER
// =============================================================================

/**
 * @brief Universal entry point for generating 2D vector geometry from centerline points.
 *
 * Dispatches between:
 * - Standard closed geometric shapes (isStandardShape == true and >= 3 points):
 *   Creates a seamless closed boundary loop where the end wraps smoothly back to the start.
 * - Open strokes (handwritten ink, lines, curves):
 *   Honors the requested terminal CapType (Round, Chisel, Square).
 *
 * @param centerline Ordered list of centerline points with radii.
 * @param capType Endcap style for open stroke endpoints.
 * @param pattern Outline pattern style (Solid, Dashed, Dotted, DashDot, TexturedPencil).
 * @param isStandardShape When true, closes the loop seamlessly from the last point back to the first.
 * @return Closed InkPath geometry ready for rasterization.
 */
InkPath InkGenerator::GenerateStroke(const std::vector<StrokePoint>& centerline,
                                      CapType capType,
                                      StrokePattern pattern,
                                      bool isStandardShape) {
    if (centerline.empty()) return InkPath{};

    if (isStandardShape && centerline.size() >= 3) {
        return BuildPolygon(centerline, pattern);
    } else {
        return BuildStroke(centerline, capType, pattern);
    }
}

// =============================================================================
// 2. CLOSED POLYGON OUTLINE BUILDER
// =============================================================================

/**
 * @brief Constructs a closed polygon stroke outline loop.
 *
 * If the input vertex sequence is open (last point != first point), this method automatically
 * closes the loop by appending a duplicate of the first point.
 * Closed polygons always use round miter corner joins and have no terminal endcaps.
 *
 * @param centerline Polygon boundary vertices.
 * @param pattern Stroke pattern.
 * @return Continuous hollow boundary ring geometry.
 */
InkPath InkGenerator::BuildPolygon(const std::vector<StrokePoint>& centerline,
                                    StrokePattern pattern) {
    if (centerline.empty()) return InkPath{};

    // Check if the input points already form a closed loop (last point == first point)
    const std::vector<StrokePoint>* ptsPtr = &centerline;
    std::vector<StrokePoint> closed;
    double dx = centerline.back().x - centerline.front().x;
    double dy = centerline.back().y - centerline.front().y;

    if (std::hypot(dx, dy) > 1e-5) {
        closed = centerline;
        closed.push_back(centerline.front());
        ptsPtr = &closed;
    }

    const auto& pts = *ptsPtr;
    if (pts.size() == 1) return TraceCap(pts[0], CapType::Round);

    // Continuous solid outline: fast-path directly to closed ribbon generator
    if (pattern == StrokePattern::Solid) {
        return BuildSolidRibbon(pts, CapType::Round, /*isClosedPolygon=*/true);
    }

    // Discontinuous patterns (Dotted, Dashed, DashDot, TexturedPencil)
    return ApplyDiscontinuousPattern(pts, pattern);
}

} // namespace Folio

