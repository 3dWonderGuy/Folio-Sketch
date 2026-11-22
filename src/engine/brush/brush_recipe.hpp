#pragma once
/**
 * =========================================================================================
 * @file src/engine/brush/brush_recipe.hpp
 * @brief Data-Driven Multi-Coat Brush Recipe & Signal Modulator Types
 * =========================================================================================
 */

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "core/ink_stroke_data.hpp"

namespace Folio {

// =============================================================================
// 1. BRUSH PRESET ENUM
// =============================================================================

enum class BrushPreset : uint8_t {
    BallpointPen = 0,
    Marker,
    FountainPen,
    Highlighter,
    Pencil,
    CalligraphyChisel,
    WetPaint,
    Graffiti,
    HolidayLights,
    TwistedYarn,
    MusicStaff,
    Custom
};

// =============================================================================
// 2. COAT EMISSION & PARTICLE ORIENTATION MODES
// =============================================================================

enum class CoatEmissionMode : uint8_t {
    ContinuousRibbon = 0, ///< Connected continuous capsule chain along centerline
    ParticleStamp    = 1, ///< Discrete tapered capsules emitted at spatial intervals along stroke
    MusicalNotes     = 2, ///< Treble clef + note heads, stems & flags composed from capsules
    YarnStrandBumps  = 3  ///< Two intertwined bumpy yarn strands (over/under weave)
};

enum class ParticleOrientation : uint8_t {
    FollowTangent          = 0, ///< Aligned along stroke tangent direction (e.g. musical notes)
    PerpendicularAlternate = 1, ///< Alternates +/- 90 degrees across tangent (e.g. holiday lights)
    GravityDown            = 2, ///< Points straight down in world space (+Y) regardless of stroke travel
    FixedAngle             = 3, ///< Fixed angle relative to canvas
    NormalHangDown         = 4  ///< Perpendicular to tangent (normal), always pointing downward (+Y >= 0)
};

enum class CoatColorMode : uint8_t {
    InheritBase = 0, ///< Use active stroke base color (subject to modulators)
    FixedColor  = 1, ///< Fixed RGBA color specified on the coat
    InvertBase  = 2  ///< Opposite / complementary hue (e.g. twisted yarn crossings)
};

enum class CoatRole : uint8_t {
    BasePigment    = 0, ///< Primary ink/pigment body coat
    UnderlayShadow = 1, ///< Halo, outline, or drop shadow coat beneath primary pigment
    GrainTexture   = 2, ///< Dry-media paper tooth or speck/splatter texture coat
    Highlight      = 3, ///< Specular, glowing, or top accent coat
    Structure      = 4  ///< Structural elements (wires, staff lines, bar lines)
};

enum class CoatBlendMode : uint8_t {
    Normal   = 0, ///< Standard premultiplied alpha-over
    Multiply = 1, ///< Darkening pigment build-up
    Add      = 2, ///< Luminous additive glow
    MaxUnion = 3  ///< Highlighter set-union (never darkens on self-overlap)
};

// =============================================================================
// 2b. BRUSH NIB (TIP PROFILE & ORIENTATION)
// =============================================================================

enum class NibShape : uint8_t {
    Round    = 0, ///< Circular stylus / ballpoint tip (isotropic)
    Oval     = 1, ///< Elliptical nib with controllable aspect ratio
    Square   = 2, ///< Rectangular felt / flat highlighter blade
    Chisel   = 3, ///< Slanted calligraphy diamond / broad-edge blade
    Custom   = 4  ///< Custom polygon or user-defined tip geometry
};

struct BrushNib {
    NibShape shape        = NibShape::Round; ///< Geometric profile
    float    aspectRatio  = 1.0f;            ///< Width-to-thickness ratio (>= 1.0; e.g. 3.0 for flat chisel)
    float    angleRad     = 0.0f;            ///< Fixed slant angle relative to canvas (e.g. -pi/4 for calligraphy)
    bool     followPath   = false;           ///< If true, rotates with stroke tangent; if false, keeps fixed canvas angle
    float    roundness    = 1.0f;            ///< Corner radius / roundness factor [0.0 = sharp polygon, 1.0 = fully rounded]
    std::string customId  = "";              ///< Optional ID for custom tip shapes / textures
};

// =============================================================================
// 3. SIGNAL MODULATOR ENUMS (SOURCE -> WAVEFORM/NOISE -> TARGET)
// =============================================================================

enum class ModulatorSource : uint8_t {
    ArcLength          = 0,  ///< Traveled path length along stroke (mm)
    Pressure           = 1,  ///< Hardware or modeled stylus pressure [0.0, 1.0]
    PressureIntegral   = 2,  ///< Accumulated integral of pressure over distance
    Velocity           = 3,  ///< Instantaneous pen travel speed (mm/s)
    Tilt               = 4,  ///< Stylus inclination angle from upright (radians)
    TravelAngle        = 5,  ///< Tangent direction angle of stroke travel (radians)
    StrokeProgress     = 6,  ///< Normalized progress along completed stroke [0.0, 1.0]
    ParticleIndex      = 7,  ///< Integer index of emitted particle
    TimeSeconds        = 8,  ///< Global elapsed time (seconds)
    TimeSinceInput     = 9,  ///< Age of point since stylus input
    TimeSinceStrokeEnd = 10, ///< Elapsed time since stroke finished (seconds)
    DistanceRemaining  = 11, ///< Distance from current point to end of stroke (mm)
    InkFlow            = 12  ///< Dynamic hydrodynamic ink flow factor [0.0, 1.0+] (velocity & corner compensated)
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
    EaseOutDripSynced  = 8, ///< EaseOutClamped(timeSinceInput) * PositiveNoise(arcLength, seed)
    BlinkPulseClamped  = 9, ///< Multi-flash luminosity pulse over [0, duration]
    TravelingWaveJelly = 10 ///< Spatial wave along stroke + EaseOut time phase
};

enum class ModulatorTarget : uint8_t {
    RadiusScale    = 0, ///< Multiplier applied to coat's evaluated stroke radius
    LateralOffset  = 1, ///< Lateral shift (mm) perpendicular to stroke normal
    WorldOffsetX   = 2, ///< Absolute X offset (mm) in world space
    WorldOffsetY   = 3, ///< Absolute Y offset (mm) in world space
    ParticleLength = 4, ///< Length multiplier for particle stamps
    HueShiftDeg    = 5, ///< Hue shift (degrees [-180, +180]) in HSL color space
    LightnessShift = 6, ///< Lightness shift [-1.0, +1.0] in HSL color space
    OpacityScale   = 7  ///< Multiplier [0.0, 1.0] applied to coat alpha / opacity
};

// =============================================================================
// 4. BRUSH MODULATOR RULE
// =============================================================================

struct BrushModulator {
    ModulatorSource   source             = ModulatorSource::ArcLength;
    ModulatorWaveform waveform           = ModulatorWaveform::SineWave;
    ModulatorTarget   target             = ModulatorTarget::LateralOffset;
    float             frequency          = 0.5f;   ///< Spatial frequency (cycles/mm or cycles/sec)
    float             periodInBrushSizes = 0.0f;   ///< If > 0, overrides frequency with 1.0 / (periodInBrushSizes * baseWidth)
    float             phase              = 0.0f;   ///< Phase offset (radians)
    float             amplitude          = 1.0f;   ///< Peak output amplitude (mm, deg, or multiplier)
    float             amplitudeRatio     = 0.0f;   ///< If > 0, scales amplitude by baseWidth
    float             bias               = 0.0f;   ///< Baseline DC offset
    uint32_t          seed               = 1337u;  ///< Seed for noise and hash generators
};

// =============================================================================
// 5. BRUSH COAT SPECIFICATION
// =============================================================================

struct BrushCoatSpec {
    std::string         name                = "Coat";
    CoatRole            role                = CoatRole::BasePigment;
    CoatBlendMode       blendMode           = CoatBlendMode::Normal;
    CoatEmissionMode    emissionMode        = CoatEmissionMode::ContinuousRibbon;
    BrushNib            nib;                 ///< Nib profile (round, oval, square, chisel, custom)
    float               widthScale          = 1.0f;
    float               radiusEndRatio      = 1.0f;
    float               particleGapMm       = 4.0f;
    float               particleGapRatio    = 0.0f;
    float               particleLengthMm    = 3.5f;
    float               particleLengthRatio = 0.0f;
    ParticleOrientation particleOrientation = ParticleOrientation::FollowTangent;
    float               particleAngleRad    = 0.0f;

    float               lateralOffsetMm            = 0.0f;
    float               lateralOffsetRatio         = 0.0f;
    bool                alternateLateralByParticle = false;
    float               startSkipBrushSizes        = 0.0f;
    float               endSkipBrushSizes          = 0.0f;
    float               worldOffsetX               = 0.0f;
    float               worldOffsetXRatio          = 0.0f;
    float               worldOffsetY               = 0.0f;
    float               worldOffsetYRatio          = 0.0f;

    CoatColorMode       colorMode           = CoatColorMode::InheritBase;
    ColorRgba           fixedColor          = ColorRgba(0, 0, 0, 255);
    float               hueOffsetDeg        = 0.0f;
    float               lightnessOffset     = 0.0f;
    float               opacityScale        = 1.0f;

    bool                triangleFan         = false;
    float               yarnPeriodBrushSizes = 3.0f;
    float               yarnAmplitudeRatio  = 0.38f;

    std::vector<BrushModulator> modulators;

    BrushCoatSpec& SetNib(NibShape shape,
                          float aspectRatio = 1.0f,
                          float angleRad = 0.0f,
                          bool followPath = false,
                          float roundness = 1.0f,
                          std::string customId = "") {
        nib.shape       = shape;
        nib.aspectRatio = std::max(aspectRatio, 0.05f);
        nib.angleRad    = angleRad;
        nib.followPath  = followPath;
        nib.roundness   = std::clamp(roundness, 0.0f, 1.0f);
        nib.customId    = std::move(customId);
        return *this;
    }

    BrushCoatSpec& SetRoundNib() {
        return SetNib(NibShape::Round, 1.0f, 0.0f, false, 1.0f);
    }

    BrushCoatSpec& SetOvalNib(float aspectRatio = 2.0f, float angleRad = 0.0f, bool followPath = false) {
        return SetNib(NibShape::Oval, aspectRatio, angleRad, followPath, 1.0f);
    }

    BrushCoatSpec& SetSquareNib(float aspectRatio = 1.0f, float angleRad = 0.0f, float roundness = 0.0f) {
        return SetNib(NibShape::Square, aspectRatio, angleRad, false, roundness);
    }

    BrushCoatSpec& SetChiselNib(float aspectRatio = 3.5f, float angleRad = -0.785398f /* -45 deg */) {
        return SetNib(NibShape::Chisel, aspectRatio, angleRad, false, 0.2f);
    }

    BrushCoatSpec& SetCustomNib(std::string customId, float aspectRatio = 1.0f, float angleRad = 0.0f, bool followPath = false) {
        return SetNib(NibShape::Custom, aspectRatio, angleRad, followPath, 1.0f, std::move(customId));
    }

    BrushCoatSpec& Bind(ModulatorSource src,
                        ModulatorTarget target,
                        ModulatorWaveform waveform = ModulatorWaveform::Linear,
                        float bias = 0.0f,
                        float amplitude = 1.0f,
                        float frequency = 1.0f,
                        float phase = 0.0f,
                        uint32_t seed = 1337u) {
        BrushModulator m;
        m.source = src;
        m.target = target;
        m.waveform = waveform;
        m.bias = bias;
        m.amplitude = amplitude;
        m.frequency = frequency;
        m.phase = phase;
        m.seed = seed;
        modulators.push_back(m);
        return *this;
    }

    BrushCoatSpec& BindPeriodic(ModulatorSource src,
                                ModulatorTarget target,
                                ModulatorWaveform waveform,
                                float periodInBrushSizes,
                                float amplitudeRatio,
                                float bias = 0.0f,
                                float phase = 0.0f,
                                uint32_t seed = 1337u) {
        BrushModulator m;
        m.source = src;
        m.target = target;
        m.waveform = waveform;
        m.periodInBrushSizes = periodInBrushSizes;
        m.bias = bias;
        m.phase = phase;
        m.seed = seed;
        if (target == ModulatorTarget::LateralOffset ||
            target == ModulatorTarget::WorldOffsetX ||
            target == ModulatorTarget::WorldOffsetY) {
            m.amplitudeRatio = amplitudeRatio;
        } else {
            m.amplitude = amplitudeRatio;
        }
        modulators.push_back(m);
        return *this;
    }

    BrushCoatSpec& BindNoiseToWidth(float minScale = 0.85f,
                                    float maxScale = 1.15f,
                                    float frequency = 0.2f,
                                    uint32_t seed = 3u) {
        return Bind(
            ModulatorSource::ArcLength,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::ValueNoise1D,
            (minScale + maxScale) * 0.5f,
            (maxScale - minScale) * 0.5f,
            frequency,
            0.0f,
            seed
        );
    }

    BrushCoatSpec& BindDirectionToWidth(float bias = 1.15f,
                                        float amplitude = 0.85f,
                                        float nibPhaseRad = -0.785398f) {
        return Bind(
            ModulatorSource::TravelAngle,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::SineWave,
            bias,
            amplitude,
            0.5f,
            nibPhaseRad
        );
    }

    BrushCoatSpec& BindPressureToWidth(float minScale = 0.2f, float maxScale = 1.5f) {
        return Bind(
            ModulatorSource::Pressure,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::Linear,
            minScale,
            maxScale - minScale,
            1.0f
        );
    }

    BrushCoatSpec& BindTiltToWidth(float minScale = 1.0f, float maxScale = 2.8f) {
        return Bind(
            ModulatorSource::Tilt,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::Linear,
            minScale,
            maxScale - minScale,
            0.63661977f // 1.0 / (pi / 2)
        );
    }

    BrushCoatSpec& BindPressureToOpacity(float minOpacity = 0.1f, float maxOpacity = 1.0f) {
        return Bind(
            ModulatorSource::Pressure,
            ModulatorTarget::OpacityScale,
            ModulatorWaveform::Linear,
            minOpacity,
            maxOpacity - minOpacity,
            1.0f
        );
    }

    BrushCoatSpec& BindTiltToOpacity(float minOpacity = 0.2f, float maxOpacity = 1.0f) {
        return Bind(
            ModulatorSource::Tilt,
            ModulatorTarget::OpacityScale,
            ModulatorWaveform::Linear,
            minOpacity,
            maxOpacity - minOpacity,
            0.63661977f // 1.0 / (pi / 2)
        );
    }

    BrushCoatSpec& BindPressureToHue(float hueDeltaDeg = 180.0f) {
        return Bind(
            ModulatorSource::Pressure,
            ModulatorTarget::HueShiftDeg,
            ModulatorWaveform::Linear,
            0.0f,
            hueDeltaDeg,
            1.0f
        );
    }

    BrushCoatSpec& BindVelocityToWidth(float minScale = 0.3f, float maxSpeedMmPerSec = 1500.0f) {
        return Bind(
            ModulatorSource::Velocity,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::Linear,
            1.0f,
            -(1.0f - minScale),
            1.0f / std::max(maxSpeedMmPerSec, 1.0f)
        );
    }

    BrushCoatSpec& BindVelocityToOpacity(float minOpacity = 0.3f, float maxSpeedMmPerSec = 1500.0f) {
        return Bind(
            ModulatorSource::Velocity,
            ModulatorTarget::OpacityScale,
            ModulatorWaveform::Linear,
            1.0f,
            -(1.0f - minOpacity),
            1.0f / std::max(maxSpeedMmPerSec, 1.0f)
        );
    }

    BrushCoatSpec& BindFlowToWidth(float minScale = 0.2f, float maxScale = 1.0f) {
        return Bind(
            ModulatorSource::InkFlow,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::Linear,
            minScale,
            maxScale - minScale,
            1.0f
        );
    }

    BrushCoatSpec& BindFlowToOpacity(float minOpacity = 0.2f, float maxOpacity = 1.0f) {
        return Bind(
            ModulatorSource::InkFlow,
            ModulatorTarget::OpacityScale,
            ModulatorWaveform::Linear,
            minOpacity,
            maxOpacity - minOpacity,
            1.0f
        );
    }

    BrushCoatSpec& BindFlowToLightness(float deltaLightnessAtMinFlow = -0.3f) {
        return Bind(
            ModulatorSource::InkFlow,
            ModulatorTarget::LightnessShift,
            ModulatorWaveform::Linear,
            deltaLightnessAtMinFlow,
            -deltaLightnessAtMinFlow,
            1.0f
        );
    }
};

// =============================================================================
// 6. BRUSH RECIPE (FULL MULTI-COAT BRUSH DEFINITION)
// =============================================================================

struct BrushRecipe {
    std::string                name;
    std::string                description;
    bool                       isAnimated          = false;
    float                      settlingDurationSec = 0.0f;
    float                      opacity             = 1.0f;
    float                      grain               = 0.0f;
    float                      grainScale          = 2.0f;
    bool                       unionOverlap        = false;
    bool                       usePressure         = true;
    bool                       useVelocity         = false;
    bool                       useTilt             = false;
    std::vector<BrushCoatSpec> coats;

    [[nodiscard]] bool empty() const noexcept { return coats.empty(); }
    [[nodiscard]] size_t CoatCount() const noexcept { return coats.size(); }

    BrushRecipe& SetTextureGrain(float grainDepth, float scale = 2.0f) noexcept {
        grain = grainDepth;
        grainScale = scale;
        return *this;
    }

    BrushCoatSpec& AddCoat(std::string coatName,
                           CoatRole role = CoatRole::BasePigment,
                           float widthScale = 1.0f) {
        BrushCoatSpec c;
        c.name = std::move(coatName);
        c.role = role;
        c.widthScale = widthScale;
        coats.push_back(std::move(c));
        return coats.back();
    }

    BrushCoatSpec& AddUnderlayCoat(std::string coatName,
                                   float widthScale,
                                   float offsetXRatio = 0.0f,
                                   float offsetYRatio = 0.0f,
                                   float opacityScale = 1.0f,
                                   float lightnessOffset = 0.0f) {
        BrushCoatSpec c;
        c.name = std::move(coatName);
        c.role = CoatRole::UnderlayShadow;
        c.widthScale = widthScale;
        c.worldOffsetXRatio = offsetXRatio;
        c.worldOffsetYRatio = offsetYRatio;
        c.opacityScale = opacityScale;
        c.lightnessOffset = lightnessOffset;
        coats.push_back(std::move(c));
        return coats.back();
    }

    BrushCoatSpec& AddParticleCoat(std::string coatName,
                                   float widthScale,
                                   float gapRatio,
                                   float lengthRatio,
                                   ParticleOrientation orientation = ParticleOrientation::FollowTangent) {
        BrushCoatSpec c;
        c.name = std::move(coatName);
        c.role = CoatRole::GrainTexture;
        c.emissionMode = CoatEmissionMode::ParticleStamp;
        c.widthScale = widthScale;
        c.particleGapRatio = gapRatio;
        c.particleLengthRatio = lengthRatio;
        c.particleOrientation = orientation;
        coats.push_back(std::move(c));
        return coats.back();
    }

    BrushRecipe& EnsureBaseCoat() {
        if (coats.empty()) {
            AddCoat("Primary Coat", CoatRole::BasePigment, 1.0f);
        }
        return *this;
    }

    BrushRecipe& BindPressureToWidth(float minScale = 0.2f, float maxScale = 1.5f) {
        EnsureBaseCoat();
        coats.front().BindPressureToWidth(minScale, maxScale);
        return *this;
    }

    BrushRecipe& BindTiltToWidth(float minScale = 1.0f, float maxScale = 2.8f) {
        EnsureBaseCoat();
        coats.front().BindTiltToWidth(minScale, maxScale);
        return *this;
    }

    BrushRecipe& BindPressureToOpacity(float minOpacity = 0.1f, float maxOpacity = 1.0f) {
        EnsureBaseCoat();
        coats.front().BindPressureToOpacity(minOpacity, maxOpacity);
        return *this;
    }

    BrushRecipe& BindPressureToHue(float hueDeltaDeg = 180.0f) {
        EnsureBaseCoat();
        coats.front().BindPressureToHue(hueDeltaDeg);
        return *this;
    }

    BrushRecipe& BindVelocityToWidth(float minScale = 0.3f, float maxSpeedMmPerSec = 1500.0f) {
        EnsureBaseCoat();
        coats.front().BindVelocityToWidth(minScale, maxSpeedMmPerSec);
        return *this;
    }

    BrushRecipe& BindFlowToWidth(float minScale = 0.2f, float maxScale = 1.0f) {
        EnsureBaseCoat();
        coats.front().BindFlowToWidth(minScale, maxScale);
        return *this;
    }

    BrushRecipe& BindFlowToOpacity(float minOpacity = 0.2f, float maxOpacity = 1.0f) {
        EnsureBaseCoat();
        coats.front().BindFlowToOpacity(minOpacity, maxOpacity);
        return *this;
    }
};

} // namespace Folio
