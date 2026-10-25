#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_generator.hpp
 * @brief Universal 2D Vector Geometry Generator (Google Ink Modeler + Blend2D)
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & TESSELLATION ALGORITHMS:
 * -----------------------------------------------
 * Constructs continuous 2D closed polygon contours (BLPath) from pressure-modeled,
 * smoothed centerline points for hardware-accelerated non-zero fill rendering via Blend2D.
 *
 * Generation Pipeline:
 *   STEP 1: Choose generation type
 *             -- Polygon (standard shapes)  -> closed loop, always Round joins, supports infill
 *             -- Stroke  (ink)              -> open line, honors CapType, no infill
 *   STEP 2: Apply line pattern to the outline (Solid, Dashed, Dotted, DashDot)
 *   STEP 3: (Polygons only) Infill — BuildInfill() region + FillInfill() paint
 *             (Solid, SemiTransparent, Linear/Radial Gradient, Drafting Hatches)
 *
 * Implementation files:
 *   - ink_generator/ink_generator_polygon.cpp  (Dispatcher & closed polygon generation)
 *   - ink_generator/ink_generator_stroke.cpp   (Open stroke & solid ribbon tessellation)
 *   - ink_generator/ink_generator_patterns.cpp (Dashed, dotted, and dash-dot algorithms)
 *   - ink_generator/ink_generator_infill.cpp   (Interior polygon infill & drafting hatches)
 */

#include <vector>
#include <string>

#include "folio_sketch/ink_path.hpp"
#include "folio_sketch/ink_stroke_data.hpp"

namespace Folio {

/**
 * @struct StrokeDebugInfo
 * @brief Detailed geometric diagnostic data captured during stroke tessellation.
 */
struct StrokeDebugInfo {
    std::vector<StrokePoint> rawInputPts;        ///< Points passed into BuildSolidRibbon
    std::vector<StrokePoint> cleanPts;           ///< Points after pre-filtering and slope clamping
    std::vector<Point2D>     leftRail;           ///< Left boundary vertices generated for the ribbon
    std::vector<Point2D>     rightRail;          ///< Right boundary vertices generated for the ribbon
    std::vector<Point2D>     selfIntersections;  ///< Exact world coordinates where non-adjacent edges intersect
    std::vector<size_t>      retrogradeIndices;  ///< Indices where boundary stepped backward against tangent
    InkPath                  builtPath;          ///< Final tessellated InkPath
    CapType                  capType = CapType::Round;
    bool                     isClosedPolygon = false;
    bool                     hasDefects = false; ///< True if selfIntersections or retrogradeIndices is non-empty
};

class InkGenerator {
public:
    // =========================================================================
    // VISUAL STROKE GEOMETRY DEBUGGER & REPRODUCER DUMPER
    // =========================================================================

    static void SetDebugCapture(bool enabled) noexcept;
    static bool IsDebugCaptureEnabled() noexcept;
    static const StrokeDebugInfo& GetLastDebugInfo() noexcept;
    static void ClearDebugInfo() noexcept;

    static void DumpLastStrokeToConsole(const StrokeDebugInfo* info = nullptr);
    static void DumpStrokeToFile(const std::string& filepath = "debug_stroke_dump.txt",
                                 const StrokeDebugInfo* info = nullptr);
    static bool DumpDebugSvg(const std::string& filepath = "debug_stroke.svg",
                             const StrokeDebugInfo* info = nullptr);

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
     * @brief Builds the interior fill region of a polygon (the area enclosed by its vertices).
     *
     * Only polygons support infill. Ink strokes never do (future stroke analysis may
     * promote enclosed ink into a polygon first).
     *
     * @param vertices Polygon perimeter vertices (open or already closed).
     * @return Closed InkPath of the enclosed area, ready for solid / gradient / hatch fill.
     */
    static InkPath BuildInfill(const std::vector<Point2D>& vertices);
    static InkPath BuildInfill(const std::vector<StrokePoint>& vertices);

    /**
     * @brief Incrementally appends newly modeled stroke segments [startIdx - 1 .. pts.size() - 1]
     *        onto an existing live InkPath in O(1) time per frame (eliminates O(N^2) live inking lag).
     *
     * @param path     Target InkPath being grown incrementally during live inking.
     * @param pts      Full modeled point buffer (must have no consecutive duplicates < 1e-4 mm).
     * @param startIdx Index of the first newly appended point in pts.
     * @param capType  Endcap / nib style.
     * @param pattern  Active stroke pattern (Solid or TexturedPencil).
     */
    static void AppendRibbonSegments(InkPath& path,
                                     const std::vector<StrokePoint>& pts,
                                     size_t startIdx,
                                     CapType capType = CapType::Round,
                                     StrokePattern pattern = StrokePattern::Solid);

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
