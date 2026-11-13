#pragma once
/**
 * =========================================================================================
 * @file src/pipelines/raster/ink_raster.hpp
 * @brief Procreate-Style Raster Dab / Stamp & Smudge Instance Pipeline
 * =========================================================================================
 *
 * Converts physics-modeled centerline points into arc-length-parameterized 2D stamp dabs
 * (Shape x Grain) with continuous sub-segment carry-over spacing, pressure/tilt dynamics,
 * deterministic scatter/jitter, and wet smudge canvas pickup weights.
 */

#include <cstdint>
#include <vector>
#include <span>
#include <type_traits>
#include "core/ink_stroke_data.hpp"
#include "pipelines/capsule/ink_capsule.hpp"

namespace Folio {

// =============================================================================
// 1. RASTER STAMP SHAPE & INSTANCE TYPES
// =============================================================================

/**
 * @enum RasterTipShape
 * @brief Built-in procedural or texture-backed stamp silhouettes.
 */
enum class RasterTipShape : uint8_t {
    RoundSoft = 0,   ///< Airbrush / radial falloff mask
    RoundHard,       ///< Crisp circular ink / gouache dab
    FlatBristle,     ///< Anisotropic flat paint / marker stamp
    ChalkSpeckle,    ///< Rough dry-media tooth stamp
    CharcoalGrain,   ///< High-contrast organic charcoal stamp
    CustomMask       ///< Host-supplied 2D shape texture mask
};

/**
 * @struct RasterDabInstance
 * @brief Tightly packed 32-byte per-dab instance streamed to GPU Raster/Smudge FBO shaders.
 */
struct RasterDabInstance {
    CapsuleVec2 center{};                   ///< Stamp center in world coordinates (mm)
    float       radius        = 0.5f;       ///< Stamp radius in world coordinates (mm)
    float       angleRad      = 0.0f;       ///< Stamp rotation angle (radians)
    uint32_t    colorRgba     = 0xFF000000u;///< Premultiplied RGBA8 pigment color
    float       hardness      = 0.8f;       ///< Radial edge hardness [0.0 = soft airbrush, 1.0 = hard]
    float       grainDepth    = 0.0f;       ///< Paper grain texture modulation depth [0.0, 1.0]
    float       smudgePull    = 0.0f;       ///< Canvas color pickup / smear weight [0.0 = pure paint, 1.0 = pure smudge]
};
static_assert(sizeof(RasterDabInstance) == 32, "RasterDabInstance must stay a tightly packed 32-byte POD");
static_assert(std::is_trivially_copyable_v<RasterDabInstance>, "RasterDabInstance must be trivially copyable");

// =============================================================================
// 2. RASTER BRUSH CONFIGURATION & INCREMENTAL CURSOR
// =============================================================================

/**
 * @struct RasterBrushConfig
 * @brief Procreate-style stamp spacing, dynamics, grain, and smudge parameters.
 */
struct RasterBrushConfig {
    RasterTipShape tipShape          = RasterTipShape::RoundSoft;
    float          spacingRatio      = 0.12f; ///< Step distance as fraction of dab diameter (e.g. 0.12 = 12% spacing)
    float          minSpacingMm      = 0.03f; ///< Hard floor on step spacing (mm) to prevent zero-step infinite loops
    float          hardness          = 0.8f;  ///< Radial edge falloff [0.0, 1.0]
    float          scatterRatio      = 0.0f;  ///< Perpendicular jitter in multiples of radius
    float          angleJitterRad    = 0.0f;  ///< Random angular jitter (radians)
    bool           alignToTangent    = true;  ///< Rotate dab along stroke travel direction
    bool           alignToTilt       = false; ///< Rotate dab along hardware stylus tilt azimuth
    float          opacityPressure   = 1.0f;  ///< How strongly stylus pressure modulates dab flow/opacity [0.0, 1.0]
    float          grainDepth        = 0.0f;  ///< Paper grain intensity [0.0, 1.0]
    float          smudgePull        = 0.0f;  ///< Smudge canvas pickup ratio [0.0 = normal brush, >0 = wet smudge]
    uint32_t       randomSeed        = 1337u; ///< Deterministic seed for scatter & rotation jitter
};

/**
 * @struct RasterWalkCursor
 * @brief Stateful arc-length accumulator for seamless O(1) incremental live dab emission.
 */
struct RasterWalkCursor {
    size_t   nextSegmentIndex = 0;     ///< Index of next segment to process
    float    distanceCarryMm  = 0.0f;  ///< Remaining arc-length distance before next dab stamp
    uint32_t dabCounter       = 0;     ///< Monotonic dab index for deterministic hash jitter
    bool     emittedFirstDab  = false; ///< True once the initial stroke-start dab has been stamped

    constexpr void Reset() noexcept {
        nextSegmentIndex = 0;
        distanceCarryMm  = 0.0f;
        dabCounter       = 0;
        emittedFirstDab  = false;
    }
};

// =============================================================================
// 3. RASTER DAB BUILDER
// =============================================================================

/**
 * @class RasterDabBuilder
 * @brief Generates GPU RasterDabInstance streams along modeled strokes via arc-length stepping.
 */
class RasterDabBuilder {
public:
    /**
     * @brief Generates the complete sequence of raster dabs for a stroke.
     */
    static void BuildDabInstances(
        std::span<const StrokePoint> points,
        ColorRgba baseColor,
        const RasterBrushConfig& config,
        std::vector<RasterDabInstance>& outDabs);

    /**
     * @brief Incremental live-stroke emitter: processes only newly added segments while
     *        carrying over sub-step arc-length remainder across frames without gaps.
     */
    static void AppendDabInstances(
        std::span<const StrokePoint> points,
        ColorRgba baseColor,
        const RasterBrushConfig& config,
        RasterWalkCursor& inoutCursor,
        std::vector<RasterDabInstance>& outDabs);
};

} // namespace Folio
