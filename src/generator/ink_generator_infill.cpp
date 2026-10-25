#include "folio_sketch/ink_generator.hpp"
#include "geometry/ink_geometry_utils.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

namespace Folio {

namespace {

// Computes polygon signed area using the Shoelace formula (Surveyor's formula)
double ComputeSignedPolygonArea(const std::vector<Point2D>& pts) {
    if (pts.size() < 3) return 0.0;
    double area = 0.0;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        area += (pts[i].x * pts[j].y) - (pts[j].x * pts[i].y);
    }
    return area * 0.5;
}

} // namespace

// =============================================================================
// 1. POLYGON INTERIOR BOUNDARY PATH
// =============================================================================

/**
 * @brief Constructs a closed boundary path representing the interior area enclosed by polygon vertices.
 *
 * If the input vertex sequence repeats the initial vertex at the end, it avoids redundant duplicate
 * line segments before issuing path.close().
 *
 * @param vertices Ordered perimeter vertices of the polygon.
 * @return Closed InkPath geometry suitable for solid or hatched fill.
 */
InkPath InkGenerator::BuildInfill(const std::vector<Point2D>& vertices) {
    InkPath path;
    if (vertices.size() < 3) return path;

    size_t count = vertices.size();
    // If last vertex is identical to the first, don't duplicate line_to before close()
    if (count > 3 && std::hypot(vertices.back().x - vertices.front().x,
                                vertices.back().y - vertices.front().y) < 1e-5) {
        --count;
    }

    path.move_to(vertices[0].x, vertices[0].y);
    for (size_t i = 1; i < count; ++i) {
        path.line_to(vertices[i].x, vertices[i].y);
    }
    path.close();
    return path;
}

/**
 * @brief Constructs a closed interior boundary path from StrokePoint vertices.
 */
InkPath InkGenerator::BuildInfill(const std::vector<StrokePoint>& vertices) {
    InkPath path;
    if (vertices.size() < 3) return path;

    size_t count = vertices.size();
    if (count > 3 && std::hypot(vertices.back().x - vertices.front().x,
                                vertices.back().y - vertices.front().y) < 1e-5) {
        --count;
    }

    path.move_to(vertices[0].x, vertices[0].y);
    for (size_t i = 1; i < count; ++i) {
        path.line_to(vertices[i].x, vertices[i].y);
    }
    path.close();
    return path;
}

// =============================================================================
// 2. SELF-LOOP ENCLOSED INFILL DETECTION
// =============================================================================

LoopInfillResult InkGenerator::DetectStrokeSelfLoop(const std::vector<StrokePoint>& stroke,
                                                    double minArea,
                                                    double maxArea) {
    LoopInfillResult result;
    if (stroke.size() < 4) return result;

    const size_t N = stroke.size();

    // Search for non-adjacent segment intersections
    // (i connects stroke[i] -> stroke[i+1], j connects stroke[j] -> stroke[j+1])
    for (size_t i = 0; i < N - 1; ++i) {
        Point2D p1(stroke[i].x, stroke[i].y);
        Point2D p2(stroke[i + 1].x, stroke[i + 1].y);

        for (size_t j = i + 2; j < N - 1; ++j) {
            // Skip immediate adjacent neighbors that share a vertex
            if (i == 0 && j + 1 == N - 1) {
                // Check if stroke ends wrap around to start
            }

            Point2D p3(stroke[j].x, stroke[j].y);
            Point2D p4(stroke[j + 1].x, stroke[j + 1].y);

            Point2D hit;
            if (InkInternal::IntersectSegments(p1, p2, p3, p4, hit, /*inclusive=*/true)) {
                // Extract loop vertices: [hit -> stroke[i+1] -> ... -> stroke[j] -> hit]
                std::vector<Point2D> loopPts;
                loopPts.reserve(j - i + 2);
                loopPts.push_back(hit);

                for (size_t k = i + 1; k <= j; ++k) {
                    loopPts.push_back(Point2D(stroke[k].x, stroke[k].y));
                }
                loopPts.push_back(hit);

                // Compute enclosed area via Shoelace formula
                double area = std::abs(ComputeSignedPolygonArea(loopPts));

                // Safety checks:
                // 1. Must be larger than minArea (filters out micro cursive loops)
                // 2. Must be smaller than maxArea (safety against flooding whole canvas)
                if (area >= minArea && area <= maxArea) {
                    result.hasLoop = true;
                    result.loopArea = area;
                    result.startSegIdx = i;
                    result.endSegIdx = j;
                    result.intersectionPoint = hit;

                    // Build closed InkPath
                    result.fillRegion = BuildInfill(loopPts);
                    result.bounds = result.fillRegion.bounds();
                    return result;
                }
            }
        }
    }

    return result;
}

} // namespace Folio




