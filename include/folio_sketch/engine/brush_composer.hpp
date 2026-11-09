#pragma once
/**
 * =========================================================================================
 * @file include/folio_sketch/engine/brush_composer.hpp
 * @brief Universal Multi-Coat & Signal-Modulator Inking Brush Composer
 * =========================================================================================
 *
 * Implements a universal, data-driven procedural brush engine inspired by Google Ink
 * (androidx.ink.brush / BrushFamily / Cahier), compiled directly into high-performance
 * GPU-instanced CapsuleInstance streams for sub-millisecond frontbuffer scanout.
 */

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/gpu/ink_mesh.hpp"

namespace Folio {

// =============================================================================
// 1. COAT EMISSION & PARTICLE ORIENTATION MODES
// =============================================================================

enum class CoatEmissionMode : uint8_t {
    ContinuousRibbon = 0, ///< Connected continuous capsule chain along centerline
    ParticleStamp    = 1  ///< Discrete tapered capsules emitted at spatial intervals along stroke
};

enum class ParticleOrientation : uint8_t {
    FollowTangent          = 0, ///< Aligned along stroke tangent direction (e.g. musical notes)
    PerpendicularAlternate = 1, ///< Alternates +/- 90 degrees across tangent (e.g. holiday lights)
    GravityDown            = 2, ///< Points straight down in world space (+Y) regardless of stroke travel (e.g. wet paint drips)
    FixedAngle             = 3  ///< Fixed angle relative to canvas
};

enum class CoatColorMode : uint8_t {
    InheritBase = 0, ///< Use active stroke base color (subject to modulators)
    FixedColor  = 1, ///< Fixed RGBA color specified on the coat
    InvertBase  = 2  ///< Opposite / complementary hue (e.g. twisted yarn crossings)
};

// =============================================================================
// 2. SIGNAL MODULATOR ENUMS (SOURCE -> WAVEFORM/NOISE -> TARGET)
// =============================================================================

enum class ModulatorSource : uint8_t {
    ArcLength          = 0,  ///< Traveled path length along stroke (mm)
    Pressure           = 1,  ///< Hardware or modeled stylus pressure [0.0, 1.0]
    PressureIntegral   = 2,  ///< Accumulated integral of pressure over distance (\int p ds)
    Velocity           = 3,  ///< Instantaneous pen travel speed (mm/s)
    Tilt               = 4,  ///< Stylus inclination angle from upright (radians)
    TravelAngle        = 5,  ///< Tangent direction angle of stroke travel (radians)
    StrokeProgress     = 6,  ///< Normalized progress along completed stroke [0.0, 1.0]
    ParticleIndex      = 7,  ///< Integer index of emitted particle
    TimeSeconds        = 8,  ///< Global elapsed time (seconds)
    TimeSinceInput     = 9,  ///< Age of point since stylus input: (currentTime - point.timestampSec)
    TimeSinceStrokeEnd = 10, ///< Elapsed time since stroke finished (seconds)
    DistanceRemaining  = 11  ///< Distance from current point to end of stroke (mm)
};

enum class ModulatorWaveform : uint8_t {
    Linear             = 0, ///< Direct scaled linear mapping: out = bias + source * amplitude
    SineWave           = 1, ///< Sinusoidal wave: out = bias + amplitude * sin(freq * source + phase)
    TriangleZigzag     = 2, ///< Symmetric triangle wave oscillating between -1 and +1
    MirrorCycle        = 3, ///< Modulo mirrored saw wave [0, 1, 0, 1] for smooth color ping-pong
    AlternateSwap      = 4, ///< Square alternate wave: flips sign (+1 / -1) every half-cycle
    ValueNoise1D       = 5, ///< Smooth 1D gradient value noise along source
    HashRandom         = 6, ///< High-frequency pseudo-random hash value [0.0, 1.0]
    EaseOutClamped     = 7, ///< Clamped cubic ease-out [0 -> 1] over duration = (1 / frequency) seconds
    EaseOutDripSynced  = 8, ///< Google wet_paint: EaseOutClamped(timeSinceInput) * PositiveNoise(arcLength, seed)
    BlinkPulseClamped  = 9, ///< Google holiday_lights: multi-flash luminosity pulse over [0, duration]
    TravelingWaveJelly = 10 ///< Google jelly_wobble: spatial wave along stroke + EaseOut time phase
};

enum class ModulatorTarget : uint8_t {
    RadiusScale    = 0, ///< Multiplier applied to coat's evaluated stroke radius
    LateralOffset  = 1, ///< Lateral shift (mm) perpendicular to stroke normal
    WorldOffsetX   = 2, ///< Absolute X offset (mm) in world space (e.g. drop shadow)
    WorldOffsetY   = 3, ///< Absolute Y offset (mm) in world space (e.g. drop shadow)
    ParticleLength = 4, ///< Length multiplier for particle stamps (e.g. dripping tear drops)
    HueShiftDeg    = 5, ///< Hue shift (degrees [-180, +180]) in HSL color space
    LightnessShift = 6, ///< Lightness shift [-1.0, +1.0] in HSL color space
    OpacityScale   = 7  ///< Multiplier [0.0, 1.0] applied to coat alpha / opacity
};

// =============================================================================
// 3. BRUSH MODULATOR RULE
// =============================================================================

struct BrushModulator {
    ModulatorSource   source             = ModulatorSource::ArcLength;
    ModulatorWaveform waveform           = ModulatorWaveform::SineWave;
    ModulatorTarget   target             = ModulatorTarget::LateralOffset;
    float             frequency          = 0.5f;   ///< Spatial frequency (cycles/mm or cycles/sec)
    float             periodInBrushSizes = 0.0f;   ///< If > 0, overrides frequency with 1.0 / (periodInBrushSizes * baseWidth)
    float             phase              = 0.0f;   ///< Phase offset (radians)
    float             amplitude          = 1.0f;   ///< Peak output amplitude (mm, deg, or multiplier)
    float             amplitudeRatio     = 0.0f;   ///< If > 0, scales amplitude by baseWidth (for spatial targets)
    float             bias               = 0.0f;   ///< Baseline DC offset
    uint32_t          seed               = 1337u;  ///< Seed for noise and hash generators
};

// =============================================================================
// 4. BRUSH COAT SPECIFICATION
// =============================================================================

struct BrushCoatSpec {
    std::string         name                = "Coat";
    CoatEmissionMode    emissionMode        = CoatEmissionMode::ContinuousRibbon;
    float               widthScale          = 1.0f;  ///< Multiplier on stroke baseWidth / radius
    float               radiusEndRatio      = 1.0f;  ///< For particles: ratio of end radius to start radius (e.g. 0.05 for sharp triangle flag)
    float               particleGapMm       = 4.0f;  ///< Fixed spacing between particle stamps (mm)
    float               particleGapRatio    = 0.0f;  ///< If > 0, spacing is in multiples of brush baseWidth
    float               particleLengthMm    = 3.5f;  ///< Fixed physical length of emitted particle (mm)
    float               particleLengthRatio = 0.0f;  ///< If > 0, length is in multiples of brush baseWidth
    ParticleOrientation particleOrientation = ParticleOrientation::FollowTangent;
    float               particleAngleRad    = 0.0f;  ///< Angular offset (radians) applied to particle direction

    // Static placement offsets
    float               lateralOffsetMm            = 0.0f;  ///< Constant offset along stroke normal (mm)
    float               lateralOffsetRatio         = 0.0f;  ///< Lateral offset in multiples of brush baseWidth
    bool                alternateLateralByParticle = false; ///< Flip lateralOffset sign (+ / -) on alternating particles
    float               startSkipBrushSizes        = 0.0f;  ///< Omit/shrink particles within this many brush sizes of start
    float               endSkipBrushSizes          = 0.0f;  ///< Omit/shrink particles within this many brush sizes of end
    float               worldOffsetX               = 0.0f;  ///< Constant drop-shadow offset X (mm)
    float               worldOffsetXRatio          = 0.0f;  ///< Drop shadow X in multiples of brush baseWidth
    float               worldOffsetY               = 0.0f;  ///< Constant drop-shadow offset Y (mm)
    float               worldOffsetYRatio          = 0.0f;  ///< Drop shadow Y in multiples of brush baseWidth

    // Color & Opacity
    CoatColorMode       colorMode           = CoatColorMode::InheritBase;
    ColorRgba           fixedColor          = ColorRgba(0, 0, 0, 255);
    float               hueOffsetDeg        = 0.0f;  ///< Constant hue shift
    float               lightnessOffset     = 0.0f;  ///< Constant lightness shift
    float               opacityScale        = 1.0f;  ///< Layer opacity multiplier [0.0, 1.0]

    // Dynamic signal modulators
    std::vector<BrushModulator> modulators;
};

// =============================================================================
// 5. BRUSH RECIPE (FULL MULTI-COAT BRUSH DEFINITION)
// =============================================================================

struct BrushRecipe {
    std::string                name;
    std::string                description;
    bool                       isAnimated          = false; ///< True if recipe uses live time modulators
    float                      settlingDurationSec = 0.0f;  ///< Post-input animation settling window (seconds, e.g. 1.0s for wet paint)
    std::vector<BrushCoatSpec> coats;

    [[nodiscard]] bool empty() const noexcept { return coats.empty(); }
};

// =============================================================================
// 6. UNIVERSAL BRUSH COMPOSER EVALUATOR & PRESETS
// =============================================================================

class BrushComposer {
public:
    /**
     * @brief Synthesizes a multi-coat BrushRecipe into an instanced GPU capsule stream.
     *
     * @param points Modeled centerline points with evaluated radii, pressure, and timestamps.
     * @param baseColor The base pigment color chosen in UI.
     * @param baseWidth Base stroke width (mm).
     * @param recipe The multi-coat brush recipe to evaluate.
     * @param currentTimeSec Current clock timestamp (seconds) for TIME_SINCE_INPUT evaluation.
     * @param outInstances Destination vector populated with GPU CapsuleInstance primitives.
     * @param strokeEndTimeSec Timestamp when pen lifted (0.0 if stroke is still actively being drawn).
     */
    static void BuildRecipeInstances(
        std::span<const StrokePoint> points,
        ColorRgba baseColor,
        float baseWidth,
        const BrushRecipe& recipe,
        double currentTimeSec,
        std::vector<CapsuleInstance>& outInstances,
        double strokeEndTimeSec = 0.0
    );

    /**
     * @brief Returns the built-in library of 12 Google Cahier brush recipes plus Standard Solid.
     */
    static std::vector<BrushRecipe> CreateCahierPresets();

    /**
     * @brief Fast HSL color conversion utilities for hue/lightness modulation.
     */
    static void RgbToHsl(ColorRgba rgb, float& hDeg, float& s, float& l) noexcept;
    static ColorRgba HslToRgb(float hDeg, float s, float l, uint8_t a = 255) noexcept;

    /**
     * @brief Evaluates smooth 1D value noise in [-1.0, 1.0].
     */
    static float Noise1D(float x, uint32_t seed) noexcept;

    /**
     * @brief Fast pseudo-random hash in [0.0, 1.0].
     */
    static float Hash1D(uint32_t index, uint32_t seed) noexcept;
};

} // namespace Folio
