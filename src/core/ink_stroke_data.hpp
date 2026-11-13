#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/core/ink_stroke_data.hpp
 * @brief Fundamental Inking Value Types & Telemetry Structures
 * =========================================================================================
 */

#include <cstdint>
#include <vector>
#include <memory>
#include <utility>

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

// =============================================================================
// 2. STYLUS TELEMETRY & CENTERLINE POINTS
// =============================================================================

/**
 * @struct StylusInputPoint
 * @brief Raw hardware sensor packet before physics smoothing.
 */
struct StylusInputPoint {
    double  x            = 0.0;   // World X coordinate (mm)
    double  y            = 0.0;   // World Y coordinate (mm)
    float   pressure     = 1.0f;  // Hardware stylus pressure [0.0, 1.0]; < 0 when unavailable
    double  timestampSec = 0.0;   // Monotonic event timestamp (seconds)
    float   speed        = 0.0f;  // Speed (mm/s)
    float   tiltX        = 0.0f;  // Stylus X tilt angle (radians)
    float   tiltY        = 0.0f;  // Stylus Y tilt angle (radians)
    int64_t timeNs       = 0;     // Hardware timestamp (nanoseconds, optional)
    float   tiltRad      = 0.0f;  // Stylus tilt altitude (radians, optional)

    // Legacy / convenience accessors
    [[nodiscard]] constexpr double TimeSec() const noexcept {
        if (timestampSec > 0.0) return timestampSec;
        if (timeNs > 0) return static_cast<double>(timeNs) * 1e-9;
        return 0.0;
    }
};

/**
 * @struct StrokePoint
 * @brief Physics-modeled centerline point with pre-calculated stroke radius.
 *        Extends StylusInputPoint with evaluated geometry radius `r`.
 */
struct StrokePoint : public StylusInputPoint {
    float r = 0.25f;  // Evaluated stroke radius / half-thickness (mm)

    constexpr StrokePoint() noexcept = default;
    constexpr StrokePoint(double x_, double y_, float r_ = 0.25f,
                          float pressure_ = 0.65f, float tiltX_ = 0.0f, float tiltY_ = 0.0f,
                          double timestampSec_ = 0.0) noexcept
        : StylusInputPoint{
            .x = x_, .y = y_, .pressure = pressure_,
            .timestampSec = timestampSec_, .speed = 0.0f,
            .tiltX = tiltX_, .tiltY = tiltY_, .timeNs = 0, .tiltRad = 0.0f
          },
          r(r_) {}

    // Implicit conversion to Point2D for geometric utilities and collision math
    constexpr operator Point2D() const noexcept { return Point2D{ x, y }; }
};

// =============================================================================
// 3. GLOBAL STROKE PATTERNS & STROKE STYLE
// =============================================================================

/**
 * @enum StrokePattern
 * @brief Visual geometric line pattern along stroke centerline.
 */
enum class StrokePattern : uint8_t {
    Solid = 0,
    Dashed,
    Dotted,
    DashDot,
    TexturedPencil,
    Custom
};

/**
 * @struct StrokeStyle
 * @brief Global styling configuration for a stroke gesture.
 */
struct StrokeStyle {
    float         baseWidth       = 0.5f;                             // Base line thickness (mm)
    StrokePattern pattern         = StrokePattern::Solid;             // Visual line pattern (Solid, Dashed, Dotted, DashDot, Custom)
    ColorRgba     color           = ColorRgba(0x18, 0x1A, 0x20, 0xFF);// Stroke pigment color
    bool          usePressure     = true;                             // Modulate width by stylus pressure
    bool          useVelocity     = false;                            // Fountain pen hydrodynamic thinning
    bool          useTilt         = false;                            // Shading spread via stylus tilt
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
