#include "folio_sketch/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

namespace Folio {

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

} // namespace Folio



