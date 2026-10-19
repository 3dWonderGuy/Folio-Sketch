#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_stroke_data.hpp
 * @brief Self-Sustained Vector Ink Geometry Data Structures
 * =========================================================================================
 *
 * Fundamental 2D point, line segment, and finished stroke data structures used
 * across vector ink stroke modeling, outline tessellation, collision testing, and rendering.
 *
 * Self-Sustained Design:
 * Holds pure geometric and telemetry value types without dependencies on external engine
 * logic, solvers, or UI palettes.
 */

#include <cstdint>
#include <vector>
#include <blend2d/blend2d.h>

/**
 * @enum CapType
 * @brief Endcap geometry style for terminal stroke ribbon segments.
 */
enum class CapType : uint8_t {
    Round = 0,
    Chisel,
    Square
};

/**
 * @enum StrokePattern
 * @brief Tessellation pattern along the stroke centerline.
 */
enum class StrokePattern : uint8_t {
    Solid = 0,
    Dashed,
    Dotted,
    DashDot,        ///< Alternating dash and dot stroke pattern
    TexturedPencil
};

/**
 * @struct StrokeStyle
 * @brief Core visual and geometric properties applied to a vector stroke or shape outline.
 */
struct StrokeStyle {
    float         baseWidth   = 0.5f;                             ///< Base line thickness in world millimeters (mm)
    CapType       capType     = CapType::Round;                   ///< Terminal segment cap style
    StrokePattern pattern     = StrokePattern::Solid;             ///< Tessellation pattern (solid, dashed, dotted, dash-dot, textured pencil)
    BLRgba32      color       = BLRgba32(0x18, 0x1A, 0x20, 0xFF); ///< Pigment color
    bool          usePressure = true;                             ///< Enable digitizer tip pressure dynamics
    bool          useTilt     = false;                            ///< Enable stylus tilt shading dynamics
    bool          useVelocity = false;                            ///< Enable hydrodynamic velocity thinning
};

/**
 * @enum InfillType
 * @brief Interior fill style for closed polygons (polygons only; ink strokes never fill).
 */
enum class InfillType : uint8_t {
    None = 0,         ///< Transparent / no fill
    Solid,            ///< Fully opaque solid color
    SemiTransparent,  ///< Translucent color wash
    LinearGradient,   ///< Two-stop gradient across the bounds diagonal
    RadialGradient,   ///< Two-stop center-outward gradient
    HatchDiagonal,    ///< 45-degree parallel drafting lines (///)
    HatchCross,       ///< Intersecting 45-degree drafting grid (XXX)
    HatchHorizontal,  ///< Horizontal parallel drafting lines (---)
    HatchVertical,    ///< Vertical parallel drafting lines (|||)
    HatchDots         ///< Fine circular dot stipple
};

/**
 * @struct InfillStyle
 * @brief Everything the generator needs to paint a polygon interior.
 */
struct InfillStyle {
    InfillType type           = InfillType::None;
    BLRgba32   primaryColor   = BLRgba32(0x00, 0x78, 0xD4, 0x40); ///< Main fill / hatch color
    BLRgba32   secondaryColor = BLRgba32(0x00, 0xC4, 0xFF, 0x20); ///< Gradient end color
    double     outlineWidthMm = 1.0;                              ///< Outline thickness (hatch lines = half of this)
    BLBox      bounds         = BLBox(0.0, 0.0, 0.0, 0.0);        ///< World-space bounds (gradients & hatch density)
};

/**
 * @struct Point2D
 * @brief Universal 2D coordinate on the infinite canvas in physical world millimeters (mm).
 */
struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

/**
 * @struct StrokePoint
 * @brief Discrete centerline vertex with pre-resolved stroke radius (r).
 */
struct StrokePoint {
    double x        = 0.0;
    double y        = 0.0;
    float  r        = 0.25f;  ///< Pre-calculated stroke radius (half-thickness) in world millimeters (mm)
    float  pressure = 0.65f;  ///< Normalized stylus pressure [0.0, 1.0] (controls pencil darkness & grain concentration)
    float  tiltX    = 0.0f;   ///< Stylus tilt angle along X axis in radians
    float  tiltY    = 0.0f;   ///< Stylus tilt angle along Y axis in radians

    constexpr StrokePoint() noexcept = default;
    constexpr StrokePoint(double x_, double y_, float r_ = 0.25f,
                          float pressure_ = 0.65f, float tiltX_ = 0.0f, float tiltY_ = 0.0f) noexcept
        : x(x_), y(y_), r(r_), pressure(pressure_), tiltX(tiltX_), tiltY(tiltY_) {}

    operator Point2D() const noexcept { return {x, y}; }
};

/**
 * @struct StylusInputPoint
 * @brief Raw hardware digitizer telemetry before physics modeling.
 */
struct StylusInputPoint {
    double x = 0.0;
    double y = 0.0;
    float  pressure = 1.0f;    ///< Stylus tip pressure [0.0, 1.0]
    double timeSeconds = 0.0;  ///< Sampling timestamp in seconds
    float  speed = 0.0f;       ///< Stylus velocity in mm/s
    float  tiltX = 0.0f;       ///< Stylus tilt angle along X axis in radians
    float  tiltY = 0.0f;       ///< Stylus tilt angle along Y axis in radians

    operator Point2D() const noexcept { return {x, y}; }
};

/**
 * @struct Segment1D
 * @brief Linear segment between two points with physical thickness (used for collision/eraser capsules).
 */
struct Segment1D {
    Point2D p0;               ///< Starting point of the segment
    Point2D p1;               ///< Ending point of the segment
    float width = 0.5f;       ///< Segment line thickness in world millimeters (mm)
};

namespace Folio {

/**
 * @struct InkVertex
 * @brief Direct GPU vertex format for SDL_GPU / OpenGL indexed triangle mesh stroke rendering.
 */
struct InkVertex {
    float x = 0.0f;              ///< World X coordinate (mm)
    float y = 0.0f;              ///< World Y coordinate (mm)
    float edgeDist = 0.0f;       ///< -1.0 (Left rail) to +1.0 (Right rail), 0.0 (Centerline) for fragment AA
    float radius = 0.0f;         ///< Stroke radius at vertex (mm)
    uint32_t color = 0xFF000000; ///< Packed RGBA8 (R in byte 0, G in byte 1, B in byte 2, A in byte 3)
};

/**
 * @struct InkMesh
 * @brief Batch of GPU-ready vertices and triangle indices.
 */
struct InkMesh {
    std::vector<InkVertex> vertices;
    std::vector<uint32_t>  indices;
    uint32_t coreIndexCount = 0; ///< Number of indices belonging to the solid core ribbon

    void clear() noexcept {
        vertices.clear();
        indices.clear();
        coreIndexCount = 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return vertices.empty() || indices.empty();
    }

    void reserve(size_t vCount, size_t iCount) {
        vertices.reserve(vCount);
        indices.reserve(iCount);
    }
};

/**
 * @brief Packs a Blend2D BLRgba32 color into R8G8B8A8 (ABGR32 little-endian) for SDL_GPU UBYTE4_NORM.
 */
[[nodiscard]] constexpr uint32_t PackGpuColor(BLRgba32 c, float opacity = 1.0f) noexcept {
    uint32_t r = c.r();
    uint32_t g = c.g();
    uint32_t b = c.b();
    uint32_t a = static_cast<uint32_t>(static_cast<float>(c.a()) * (opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity)) + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

} // namespace Folio

using InkVertex = Folio::InkVertex;
using InkMesh   = Folio::InkMesh;

/**
 * @struct FinishedStrokeData
 * @brief Completed inking stroke container encapsulating collision segments, modeled centerline, outline polygon, and GPU mesh.
 */
struct FinishedStrokeData {
    std::vector<Segment1D> liveSegments;
    std::vector<StrokePoint> modeledPoints;
    BLPath outlinePath;
    Folio::InkMesh mesh;
};