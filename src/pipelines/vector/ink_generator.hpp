#pragma once
/**
 * =========================================================================================
 * @file src/pipelines/vector/ink_generator.hpp
 * @brief Offline 2D Vector Outline & Ribbon Geometry Generator (PDF / SVG / Clipboard Export)
 * =========================================================================================
 *
 * Constructs continuous 2D closed polygon contours (`InkPath`) from finalized centerline
 * points (`StrokePoint`) for vector export (PDF, SVG, clipboard).
 *
 * Implementation files:
 *   - ink_generator_polygon.cpp  (Dispatcher & closed polygon generation)
 *   - ink_generator_stroke.cpp   (Open stroke & pattern dispatch)
 *   - ink_generator_ribbon.cpp   (Solid ribbon outline tessellation)
 *   - ink_generator_caps.cpp     (Round, Chisel, and Square endcap tracing)
 *   - ink_generator_patterns.cpp (Dashed, dotted, dash-dot, and pencil contours)
 */

#include <vector>

#include "pipelines/vector/ink_path.hpp"
#include "core/ink_stroke_data.hpp"

namespace Folio {

/**
 * @enum CapType
 * @brief Terminal cap geometry for vector outline generation (SVG/PDF ribbons).
 */
enum class CapType : uint8_t {
    Round = 0,
    Chisel,
    Square,
    Custom
};

class InkGenerator {
public:
    /**
     * @brief Builds a continuous 2D closed ribbon polygon from an ordered list of points.
     */
    static InkPath BuildSolidRibbon(const std::vector<StrokePoint>& pts,
                                    CapType capType = CapType::Round,
                                    bool isClosedPolygon = false);

    /**
     * @brief Builds a 2D closed ribbon polygon or patterned outline (solid, dashed, dotted) from an ordered list of points.
     *
     * @param centerline Input centerline vertices with radius (r).
     * @param capType Endcap style for terminal segments (ink strokes only; polygons are always Round).
     * @param pattern Stroke pattern (Solid, Dashed, Dotted, DashDot).
     * @param isStandardShape When true (standard vector shape), closes the loop from the last point to the first point seamlessly.
     */
    static InkPath GenerateStroke(const std::vector<StrokePoint>& centerline,
                                  CapType capType = CapType::Round,
                                  StrokePattern pattern = StrokePattern::Solid,
                                  bool isStandardShape = false);

    /**
     * @brief Universal cap tracer: generates a full closed cap polygon for a single-point tap.
     *
     * @param pt Centerline point with radius (r).
     * @param capType Cap style (Round, Chisel, Square).
     */
    static InkPath TraceCap(const StrokePoint& pt,
                            CapType capType = CapType::Round);

    /**
     * @brief Universal endcap tracer: connects ribbon edges across the terminal cap.
     */
    static void TraceCap(InkPath& path,
                         const StrokePoint& center,
                         const Point2D& leftPt,
                         const Point2D& rightPt,
                         CapType capType = CapType::Round,
                         bool isTail = true,
                         double dirX = 0.0,
                         double dirY = 0.0);

private:
    // =========================================================================
    // STEP 1: GENERATION TYPE (Polygon vs Stroke)
    // =========================================================================

    // Builds an open ink stroke. Continuous lines route directly to ribbon generation
    static InkPath BuildStroke(const std::vector<StrokePoint>& centerline,
                               CapType capType = CapType::Round,
                               StrokePattern pattern = StrokePattern::Solid);

    // Builds a polygon from vertices, ensuring the loop is closed seamlessly
    static InkPath BuildPolygon(const std::vector<StrokePoint>& centerline,
                                StrokePattern pattern = StrokePattern::Solid);

    // =========================================================================
    // STEP 2: STROKE & RIBBON TESSELLATION
    // =========================================================================

    // Dispatches non-solid discontinuous patterns (Dotted, Dashed, DashDot)
    static InkPath ApplyDiscontinuousPattern(const std::vector<StrokePoint>& pts,
                                             StrokePattern pattern);

    // Generates a dotted stroke contour spaced proportionally to stroke thickness
    static InkPath BuildDottedPattern(const std::vector<StrokePoint>& pts);

    // Generates an arc-length parameterized dashed stroke contour
    static InkPath BuildDashedPattern(const std::vector<StrokePoint>& pts);

    // Generates an arc-length parameterized dash-dot sequence contour
    static InkPath BuildDashDotPattern(const std::vector<StrokePoint>& pts);

    // Generates a graphite pencil stroke contour with paper tooth micro-texture and fiber striations
    static InkPath BuildTexturedPencilPattern(const std::vector<StrokePoint>& pts);
};

} // namespace Folio
