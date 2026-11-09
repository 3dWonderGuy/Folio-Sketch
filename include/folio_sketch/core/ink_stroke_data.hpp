#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/core/ink_stroke_data.hpp
 * @brief Fundamental Inking Value Types & Telemetry Structures
 * =========================================================================================
 */

#include <cstdint>
#include <vector>

namespace Folio {

// =============================================================================
// 1. PRIMITIVES: COORDINATES & COLOR
// =============================================================================

/**
 * @struct Point2D
 * @brief 2D canvas coordinate in physical world millimeters (mm).
 */
struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

/**
 * @struct ColorRgba
 * @brief 32-bit RGBA color representation (0 to 255 per channel).
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
          g(static_cast<uint8_t>((argb >> 8)  & 0xFF)),
          b(static_cast<uint8_t>(argb         & 0xFF)),
          a(static_cast<uint8_t>((argb >> 24) & 0xFF)) {}

    [[nodiscard]] constexpr uint32_t toU32() const noexcept {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(b) << 16) |
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

// =============================================================================
// 2. STYLUS TELEMETRY & CENTERLINE POINTS
// =============================================================================

/**
 * @struct StylusInputPoint
 * @brief Raw hardware sensor packet before physics smoothing.
 */
struct StylusInputPoint {
    double x           = 0.0;   // World X coordinate (mm)
    double y           = 0.0;   // World Y coordinate (mm)
    float  pressure    = 1.0f;  // Hardware stylus pressure [0.0, 1.0]
    double timeSeconds = 0.0;   // Timestamp (seconds)
    float  speed       = 0.0f;  // Speed (mm/s)
    float  tiltX       = 0.0f;  // Stylus X tilt angle (radians)
    float  tiltY       = 0.0f;  // Stylus Y tilt angle (radians)

    constexpr StylusInputPoint() noexcept = default;
    constexpr StylusInputPoint(double x_, double y_, float pressure_ = 1.0f,
                               double timeSec_ = 0.0, float speed_ = 0.0f,
                               float tiltX_ = 0.0f, float tiltY_ = 0.0f) noexcept
        : x(x_), y(y_), pressure(pressure_),
          timeSeconds(timeSec_), speed(speed_),
          tiltX(tiltX_), tiltY(tiltY_) {}
};

/**
 * @struct StrokePoint
 * @brief Physics-modeled centerline point with pre-calculated stroke radius.
 */
struct StrokePoint {
    double x            = 0.0;    // Smoothed world X (mm)
    double y            = 0.0;    // Smoothed world Y (mm)
    float  r            = 0.25f;  // Evaluated stroke radius / half-thickness (mm)
    float  pressure     = 0.65f;  // Modeled pressure [0.0, 1.0]
    float  tiltX        = 0.0f;   // Stylus X tilt angle (radians)
    float  tiltY        = 0.0f;   // Stylus Y tilt angle (radians)
    double timestampSec = 0.0;    // Input birth timestamp (seconds) for live TIME_SINCE_INPUT animations

    constexpr StrokePoint() noexcept = default;
    constexpr StrokePoint(double x_, double y_, float r_ = 0.25f,
                          float pressure_ = 0.65f, float tiltX_ = 0.0f, float tiltY_ = 0.0f,
                          double timestampSec_ = 0.0) noexcept
        : x(x_), y(y_), r(r_), pressure(pressure_), tiltX(tiltX_), tiltY(tiltY_),
          timestampSec(timestampSec_) {}

    // Implicit conversion to Point2D for geometric utilities and collision math
    constexpr operator Point2D() const noexcept { return Point2D{ x, y }; }
};

// =============================================================================
// 3. STROKE STYLES, CAPS & PATTERNS
// =============================================================================

/**
 * @enum CapType
 * @brief Terminal cap geometry for strokes.
 */
enum class CapType : uint8_t {
    Round = 0,
    Chisel,
    Square
};

/**
 * @enum StrokePattern
 * @brief Visual pattern along stroke centerline.
 */
enum class StrokePattern : uint8_t {
    Solid = 0,
    Dashed,
    Dotted,
    DashDot,
    TexturedPencil
};

/**
 * @struct StrokeStyle
 * @brief Tool preset and aesthetic configuration for a stroke.
 */
struct StrokeStyle {
    float         baseWidth   = 0.5f;                             // Base line thickness (mm)
    CapType       capType     = CapType::Round;                   // Terminal cap style
    StrokePattern pattern     = StrokePattern::Solid;             // Pattern (solid, dash, dot, pencil)
    ColorRgba     color       = ColorRgba(0x18, 0x1A, 0x20, 0xFF);// Stroke pigment color
    bool          usePressure = true;                             // Modulate width by stylus pressure
    bool          useVelocity = false;                            // Fountain pen hydrodynamic thinning
    bool          useTilt     = false;                            // Shading spread via stylus tilt
    float         nibAngleRad = 0.0f;                             // Orientation of the nib's major axis (radians, canvas space)
    float         nibAspect   = 1.0f;                             // Minor/major axis ratio of the elliptical nib; 1 = round (isotropic)
};

// =============================================================================
// 4. CLOSED POLYGON INFILL STYLES
// =============================================================================

/**
 * @enum InfillType
 * @brief Interior fill pattern for closed vector shapes.
 */
enum class InfillType : uint8_t {
    None = 0,         // No interior fill (outline only)
    Solid,            // Uniform color fill (alpha in ColorRgba controls opacity)
    HatchDiagonal,    // 45-degree diagonal drafting lines
    HatchCross,       // Intersecting drafting cross-hatch
    HatchHorizontal,  // Horizontal drafting lines
    HatchVertical,    // Vertical drafting lines
    HatchDots         // Stipple dot pattern
};

/**
 * @struct InfillStyle
 * @brief Infill appearance configuration for closed polygons.
 */
struct InfillStyle {
    InfillType type           = InfillType::None;
    ColorRgba  color          = ColorRgba(0x00, 0x78, 0xD4, 0x40); // Fill / hatch color (with alpha)
    double     outlineWidthMm = 1.0;                               // Reference outline thickness
};

// =============================================================================
// 5. COLLISION & FINISHED DATA CONTAINERS
// =============================================================================

/**
 * @struct Segment1D
 * @brief Linear segment between two points with physical width (capsule collision).
 */
struct Segment1D {
    Point2D p0;
    Point2D p1;
    float   width = 0.5f;
};

} // namespace Folio
