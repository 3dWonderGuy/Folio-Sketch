#pragma once
/**
 * =========================================================================================
 * @file src/pipelines/raster/ink_raster.hpp
 * @brief Procreate-Style Raster Dab / Stamp & Smudge Instance Pipeline
 * =========================================================================================
 *
 * Converts physics-modeled centerline points into arc-length-parameterized 2D stamp dabs
 * (Shape x Grain) with continuous sub-segment carry-over spacing, pressure/tilt dynamics,
 * paint reservoir depletion, deterministic scatter/jitter, and wet smudge canvas pickup weights.
 */

#include <cstdint>
#include <vector>
#include <span>
#include <type_traits>
#include "core/canvas_material.hpp"
#include "core/ink_stroke_data.hpp"
#include "pipelines/capsule/ink_capsule.hpp"

namespace Folio {

// =============================================================================
// 1. RASTER STAMP SHAPE, PRESET & INSTANCE TYPES
// =============================================================================

/**
 * @enum RasterTipShape
 * @brief Built-in procedural or texture-backed stamp silhouettes.
 */
enum class RasterTipShape : uint8_t {
    RoundSoft = 0,   ///< Airbrush / radial falloff mask
    RoundHard,       ///< Crisp circular ink / gouache dab
    FlatBristle,     ///< Anisotropic flat paint / bristle cluster stamp
    ChalkSpeckle,    ///< Rough dry-media tooth stamp
    CharcoalGrain,   ///< High-contrast organic charcoal stamp
    CustomMask       ///< Host-supplied 2D shape texture mask
};

/**
 * @enum ProBrushPreset
 * @brief Curated Procreate-style pro media presets using Shape x Material Grain x Paint Dynamics.
 */
enum class ProBrushPreset : uint8_t {
    HB_Pencil = 0,
    VineCharcoal,
    SoftPastel,
    OilFlatBristle,
    AcrylicImpasto,
    WatercolorWash,
    DryBrush,
    AerosolSprayCan,
    PaintSplatter,
    WetGouacheMixer,
    PaletteKnife,
    Custom
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
 * @brief Procreate-style stamp spacing, anisotropy, tilt spread, reservoir depletion, grain, and smudge parameters.
 */
struct RasterBrushConfig {
    RasterTipShape tipShape             = RasterTipShape::RoundSoft;
    float          spacingRatio         = 0.12f; ///< Step distance as fraction of dab diameter (e.g. 0.12 = 12% spacing)
    float          minSpacingMm         = 0.03f; ///< Hard floor on step spacing (mm) to prevent zero-step infinite loops
    float          hardness             = 0.8f;  ///< Radial edge falloff [0.0, 1.0]
    float          aspectRatio          = 1.0f;  ///< Minor/major axis ratio for flat bristles & chisel tips [0.1, 1.0]
    float          scatterRatio         = 0.0f;  ///< Perpendicular jitter in multiples of radius
    float          angleJitterRad       = 0.0f;  ///< Random angular jitter (radians)
    float          sizeJitter           = 0.0f;  ///< Random per-dab radius variation [0.0, 1.0]
    float          opacityJitter        = 0.0f;  ///< Random per-dab opacity variation [0.0, 1.0]
    float          hueJitterDeg         = 0.0f;  ///< Random per-dab hue variation (degrees)
    float          lightnessJitter      = 0.0f;  ///< Random per-dab lightness variation [0.0, 0.5]
    bool           alignToTangent       = true;  ///< Rotate dab along stroke travel direction
    bool           alignToTilt          = false; ///< Rotate dab along hardware stylus tilt azimuth
    float          tiltSpreadMultiplier = 1.0f;  ///< How strongly stylus tilt widens dab & increases grain skimming [1.0, 3.5]
    float          opacityPressure      = 1.0f;  ///< How strongly stylus pressure modulates dab flow/opacity [0.0, 1.0]
    float          grainDepth           = 0.0f;  ///< Paper grain intensity [0.0, 1.0]
    float          wetEdge              = 0.0f;  ///< Watercolor pigment pooling along outer dab rim [0.0, 1.0]
    float          chargeDistanceMm     = 0.0f;  ///< 0.0 = infinite ink reservoir; >0 = distance (mm) over which paint depletes
    float          minDepletedOpacity   = 0.05f; ///< Floor opacity when paint reservoir is depleted [0.0, 1.0]
    float          velocityThinning     = 0.0f;  ///< Speed starvation factor [0.0, 1.0]
    float          smudgePull           = 0.0f;  ///< Smudge canvas pickup ratio [0.0 = normal brush, >0 = wet smudge]
    uint32_t       randomSeed           = 1337u; ///< Deterministic seed for scatter & rotation jitter
};

/**
 * @struct RasterWalkCursor
 * @brief Stateful arc-length & paint reservoir accumulator for seamless O(1) incremental live dab emission.
 */
struct RasterWalkCursor {
    size_t   nextSegmentIndex   = 0;     ///< Index of next segment to process
    float    distanceCarryMm    = 0.0f;  ///< Remaining arc-length distance before next dab stamp
    uint32_t dabCounter         = 0;     ///< Monotonic dab index for deterministic hash jitter
    bool     emittedFirstDab    = false; ///< True once the initial stroke-start dab has been stamped
    float    remainingCharge    = 1.0f;  ///< Remaining paint load in reservoir [0.0 = dry, 1.0 = freshly loaded]
    float    traveledDistanceMm = 0.0f;  ///< Total arc length traveled by this stroke (mm)

    constexpr void Reset() noexcept {
        nextSegmentIndex   = 0;
        distanceCarryMm    = 0.0f;
        dabCounter         = 0;
        emittedFirstDab    = false;
        remainingCharge    = 1.0f;
        traveledDistanceMm = 0.0f;
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
     * @brief Generates the complete sequence of raster dabs for a stroke over a surface material.
     */
    static void BuildDabInstances(
        std::span<const StrokePoint> points,
        ColorRgba baseColor,
        const RasterBrushConfig& config,
        std::vector<RasterDabInstance>& outDabs,
        const CanvasMaterial& material = CanvasMaterial::SmoothPaper());

    /**
     * @brief Incremental live-stroke emitter: processes only newly added segments while
     *        carrying over sub-step arc-length remainder and paint reservoir charge across frames.
     */
    static void AppendDabInstances(
        std::span<const StrokePoint> points,
        ColorRgba baseColor,
        const RasterBrushConfig& config,
        RasterWalkCursor& inoutCursor,
        std::vector<RasterDabInstance>& outDabs,
        const CanvasMaterial& material = CanvasMaterial::SmoothPaper());

    /**
     * @brief Returns a tuned RasterBrushConfig for the requested ProBrushPreset.
     */
    [[nodiscard]] static RasterBrushConfig GetProBrushPreset(ProBrushPreset preset) noexcept;

    /**
     * @brief Returns a human-readable UI name for a ProBrushPreset.
     */
    [[nodiscard]] static const char* GetProBrushPresetName(ProBrushPreset preset) noexcept;
};

} // namespace Folio
