#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/ink_stroke_data.hpp
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

#include "folio_sketch/ink_path.hpp"

namespace Folio {

/**
 * @struct ColorRgba
 * @brief Self-sustained 32-bit RGBA color representation.
 */
struct ColorRgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr ColorRgba() noexcept = default;
    constexpr ColorRgba(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) noexcept
        : r(r_), g(g_), b(b_), a(a_) {}
    constexpr ColorRgba(uint32_t argb) noexcept
        : r(static_cast<uint8_t>((argb >> 16) & 0xFF)),
          g(static_cast<uint8_t>((argb >> 8) & 0xFF)),
          b(static_cast<uint8_t>(argb & 0xFF)),
          a(static_cast<uint8_t>((argb >> 24) & 0xFF)) {}

    // Transparent interoperability with Blend2D BLRgba32 or any {r, g, b, a} struct
    template <typename TColor, typename = std::enable_if_t<
        std::is_class_v<TColor> &&
        !std::is_same_v<std::decay_t<TColor>, ColorRgba>>>
    constexpr ColorRgba(const TColor& c) noexcept
        : r(static_cast<uint8_t>(c.r)),
          g(static_cast<uint8_t>(c.g)),
          b(static_cast<uint8_t>(c.b)),
          a(static_cast<uint8_t>(c.a)) {}

    [[nodiscard]] constexpr uint8_t red() const noexcept { return r; }
    [[nodiscard]] constexpr uint8_t green() const noexcept { return g; }
    [[nodiscard]] constexpr uint8_t blue() const noexcept { return b; }
    [[nodiscard]] constexpr uint8_t alpha() const noexcept { return a; }

    constexpr void setR(uint8_t v) noexcept { r = v; }
    constexpr void setG(uint8_t v) noexcept { g = v; }
    constexpr void setB(uint8_t v) noexcept { b = v; }
    constexpr void setA(uint8_t v) noexcept { a = v; }

    [[nodiscard]] constexpr uint32_t toU32() const noexcept {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(b)  << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(r);
    }

    constexpr bool operator==(const ColorRgba& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    constexpr bool operator!=(const ColorRgba& o) const noexcept {
        return !(*this == o);
    }
};

using Color = ColorRgba;

/**
 * @struct Point2D
 * @brief Universal 2D coordinate on the infinite canvas in physical world millimeters (mm).
 */
struct Point2D {
    double x = 0.0;
    double y = 0.0;

    constexpr Point2D() noexcept = default;
    constexpr Point2D(double x_, double y_) noexcept : x(x_), y(y_) {}
};

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
    ColorRgba     color       = ColorRgba(0x18, 0x1A, 0x20, 0xFF); ///< Pigment color
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
    InfillType    type           = InfillType::None;
    ColorRgba     primaryColor   = ColorRgba(0x00, 0x78, 0xD4, 0x40); ///< Main fill / hatch color
    ColorRgba     secondaryColor = ColorRgba(0x00, 0xC4, 0xFF, 0x20); ///< Gradient end color
    double        outlineWidthMm = 1.0;                               ///< Outline thickness (hatch lines = half of this)
    BoundingBox2D bounds         = BoundingBox2D(0.0, 0.0, 0.0, 0.0); ///< World-space bounds (gradients & hatch density)
};

/**
 * @struct StylusTelemetry
 * @brief Stylus digitizer sensor dynamics (pressure, tilt angles).
 */
struct StylusTelemetry {
    float pressure = 1.0f;    ///< Stylus tip contact pressure [0.0, 1.0]
    float tiltX    = 0.0f;    ///< Stylus tilt angle along X axis in radians
    float tiltY    = 0.0f;    ///< Stylus tilt angle along Y axis in radians

    constexpr StylusTelemetry() noexcept = default;
    constexpr StylusTelemetry(float pressure_, float tiltX_ = 0.0f, float tiltY_ = 0.0f) noexcept
        : pressure(pressure_), tiltX(tiltX_), tiltY(tiltY_) {}
};

/**
 * @struct StrokePoint
 * @brief Discrete centerline vertex with pre-resolved stroke radius (r).
 */
struct StrokePoint : public Point2D, public StylusTelemetry {
    float r = 0.25f;  ///< Pre-calculated stroke radius (half-thickness) in world millimeters (mm)

    constexpr StrokePoint() noexcept = default;
    constexpr StrokePoint(double x_, double y_, float r_ = 0.25f,
                          float pressure_ = 0.65f, float tiltX_ = 0.0f, float tiltY_ = 0.0f) noexcept
        : Point2D(x_, y_), StylusTelemetry(pressure_, tiltX_, tiltY_), r(r_) {}
};

/**
 * @struct StylusInputPoint
 * @brief Raw hardware digitizer telemetry before physics modeling.
 */
struct StylusInputPoint : public Point2D, public StylusTelemetry {
    double timeSeconds = 0.0;  ///< Sampling timestamp in seconds
    float  speed = 0.0f;       ///< Stylus velocity in mm/s

    constexpr StylusInputPoint() noexcept = default;
    constexpr StylusInputPoint(double x_, double y_, float pressure_ = 1.0f,
                               double timeSec_ = 0.0, float speed_ = 0.0f,
                               float tiltX_ = 0.0f, float tiltY_ = 0.0f) noexcept
        : Point2D(x_, y_), StylusTelemetry(pressure_, tiltX_, tiltY_),
          timeSeconds(timeSec_), speed(speed_) {}
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
 * @brief Packs a ColorRgba color into R8G8B8A8 (ABGR32 little-endian) for GPU vertex color.
 */
[[nodiscard]] constexpr uint32_t PackGpuColor(ColorRgba c, float opacity = 1.0f) noexcept {
    uint32_t r = c.r;
    uint32_t g = c.g;
    uint32_t b = c.b;
    uint32_t a = static_cast<uint32_t>(static_cast<float>(c.a) * (opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity)) + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/**
 * @struct FinishedStrokeData
 * @brief Completed inking stroke container encapsulating collision segments, modeled centerline, outline polygon, and GPU mesh.
 */
struct FinishedStrokeData {
    std::vector<Segment1D> liveSegments;
    std::vector<StrokePoint> modeledPoints;
    InkPath outlinePath;
    InkMesh mesh;

    FinishedStrokeData() = default;
};

} // namespace Folio
