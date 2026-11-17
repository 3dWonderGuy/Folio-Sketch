/**
 * =========================================================================================
 * @file src/engine/brush/brush_modulator.cpp
 * @brief Signal Modulators, Transfer Functions, and Waveform Evaluators for Brush Recipes
 * =========================================================================================
 *
 * Implements the mathematical signal processing core of the brush system:
 * - Telemetry Extraction: Queries physical stylus inputs (pressure, tilt, velocity, distance).
 * - Transfer Functions / Waveforms: Evaluates trigonometric, noise, ease, and harmonic functions.
 * - Coat Modulator Evaluation: Computes the aggregated radius, offset, opacity, and color shifts.
 * - Color / Noise Utilities: RGB <-> HSL color spaces, 1D Hermite noise, and integer hash PRNGs.
 */

#include "engine/brush/brush_modulator.hpp"
#include <algorithm>
#include <cmath>

namespace Folio {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

/**
 * @brief Integer hash function mapping an index and seed to a uniform float in [0.0, 1.0].
 *
 * Uses MurmurHash3's 32-bit finalizer avalanche constants (fmix32) to produce
 * high-entropy, uncorrelated pseudo-random values with zero memory allocation.
 *
 * @param index Ordinal particle/dab index or integer coordinate.
 * @param seed Modulator-specific randomization seed.
 * @return Normalized float in range [0.0, 1.0].
 */
float ModulatorEvaluator::Hash1D(uint32_t index, uint32_t seed) noexcept {
    uint32_t h = index * 0x85ebca6bu ^ seed;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

/**
 * @brief Smooth 1D continuous value noise with Hermite cubic interpolation.
 *
 * Evaluates pseudo-random lattice values at floor(x) and floor(x)+1, interpolating
 * between them using the smoothstep S-curve u = f * f * (3 - 2 * f) to eliminate
 * derivative discontinuities along stroke trajectories.
 *
 * @param x Spatial coordinate along stroke path (typically normalized by wavelength).
 * @param seed Modulator randomization seed.
 * @return Continuous noise value in range [-1.0, +1.0].
 */
float ModulatorEvaluator::Noise1D(float x, uint32_t seed) noexcept {
    int i = static_cast<int>(std::floor(x));
    float f = x - static_cast<float>(i);
    float u = f * f * (3.0f - 2.0f * f); // Hermite cubic curve

    float h0 = Hash1D(static_cast<uint32_t>(i), seed) * 2.0f - 1.0f;
    float h1 = Hash1D(static_cast<uint32_t>(i + 1), seed) * 2.0f - 1.0f;
    return (1.0f - u) * h0 + u * h1;
}

/**
 * @brief Converts an RGBA color into the Hue-Saturation-Lightness (HSL) cylindrical color space.
 *
 * Used for independent manipulation of hue (chromatic angle) and lightness without
 * distorting color saturation or alpha channels during live inking.
 *
 * @param rgb Input 32-bit color (sRGB color channels).
 * @param[out] hDeg Output hue angle in degrees [0.0, 360.0).
 * @param[out] s Output saturation normalized in [0.0, 1.0].
 * @param[out] l Output lightness normalized in [0.0, 1.0].
 */
void ModulatorEvaluator::RgbToHsl(ColorRgba rgb, float& hDeg, float& s, float& l) noexcept {
    float r = static_cast<float>(rgb.r) / 255.0f;
    float g = static_cast<float>(rgb.g) / 255.0f;
    float b = static_cast<float>(rgb.b) / 255.0f;

    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    l = (maxVal + minVal) * 0.5f;

    if (delta < 1e-5f) {
        hDeg = 0.0f;
        s = 0.0f;
        return;
    }

    s = (l > 0.5f) ? (delta / (2.0f - maxVal - minVal)) : (delta / (maxVal + minVal));

    if (maxVal == r) {
        hDeg = 60.0f * (std::fmod((g - b) / delta, 6.0f));
    } else if (maxVal == g) {
        hDeg = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        hDeg = 60.0f * (((r - g) / delta) + 4.0f);
    }

    if (hDeg < 0.0f) hDeg += 360.0f;
}

/**
 * @brief Converts Hue-Saturation-Lightness (HSL) coordinates back to 8-bit RGBA color channels.
 *
 * @param hDeg Hue angle in degrees (automatically modulo-wrapped).
 * @param s Saturation in [0.0, 1.0].
 * @param l Lightness in [0.0, 1.0].
 * @param a Alpha transparency channel [0, 255].
 * @return Reconstructed 8-bit RGBA color struct.
 */
ColorRgba ModulatorEvaluator::HslToRgb(float hDeg, float s, float l, uint8_t a) noexcept {
    hDeg = std::fmod(hDeg, 360.0f);
    if (hDeg < 0.0f) hDeg += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    l = std::clamp(l, 0.0f, 1.0f);

    float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::abs(std::fmod(hDeg / 60.0f, 2.0f) - 1.0f));
    float m = l - c * 0.5f;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hDeg < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (hDeg < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (hDeg < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (hDeg < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (hDeg < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }

    return ColorRgba(
        static_cast<uint8_t>(std::clamp((r + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((g + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((b + m) * 255.0f, 0.0f, 255.0f)),
        a
    );
}

/**
 * @brief Evaluates all signal modulators bound to a specific coat layer at a single point.
 *
 * Iterates through the coat's modulator pipeline:
 * 1. Resolves input telemetry source value (e.g. pressure, tilt, arc-length).
 * 2. Scales frequency (supporting absolute cycles/mm or brush-width proportional periods).
 * 3. Evaluates waveform transfer function (sine, noise, ease-out, linear, etc.).
 * 4. Multiplies by amplitude and adds bias.
 * 5. Applies result to the requested visual target property (radius, offsets, hue, opacity).
 *
 * @param coat The brush coat specification containing baseline geometry and modulators.
 * @param ctx Snapshot of instantaneous kinematics and timestamps at this point.
 * @param baseWidth Active base brush tip thickness (mm).
 * @return EvaluatedMods struct containing final computed multipliers and offsets.
 */
EvaluatedMods ModulatorEvaluator::EvaluateCoatModulators(
    const BrushCoatSpec& coat,
    const PointContext& ctx,
    float baseWidth) noexcept {

    EvaluatedMods res;
    // Apply baseline coat offsets
    res.lateralOffset = coat.lateralOffsetMm + coat.lateralOffsetRatio * baseWidth;
    if (coat.alternateLateralByParticle && (static_cast<uint32_t>(ctx.particleIndex) % 2 != 0)) {
        res.lateralOffset = -res.lateralOffset;
    }
    res.worldOffsetX   = coat.worldOffsetX + coat.worldOffsetXRatio * baseWidth;
    res.worldOffsetY   = coat.worldOffsetY + coat.worldOffsetYRatio * baseWidth;
    res.hueShiftDeg    = coat.hueOffsetDeg;
    res.lightnessShift = coat.lightnessOffset;
    res.opacityScale   = coat.opacityScale;

    // Process each active signal modulator rule
    for (const auto& mod : coat.modulators) {
        float srcVal = GetSourceValue(mod.source, ctx);
        // Frequency in cycles/unit, or wavelength defined as a multiple of brush size
        float effFreq = (mod.periodInBrushSizes > 0.0f)
            ? (1.0f / (mod.periodInBrushSizes * std::max(baseWidth, 0.1f)))
            : mod.frequency;
        float rawLinearArg = effFreq * srcVal;
        float phaseArg = rawLinearArg * kTwoPi + mod.phase;
        float wave = EvaluateWaveform(mod.waveform, phaseArg, rawLinearArg, ctx, mod.seed);

        // Amplitude and bias may scale proportionally with brush tip thickness
        float effAmp = (mod.amplitudeRatio != 0.0f) ? (mod.amplitudeRatio * baseWidth) : mod.amplitude;
        float effBias = (mod.amplitudeRatio != 0.0f) ? (mod.bias * baseWidth) : mod.bias;
        float outVal = effBias + effAmp * wave;

        // Route output to target channel
        switch (mod.target) {
            case ModulatorTarget::RadiusScale:
                res.radiusScale = std::max(0.01f, res.radiusScale * outVal);
                break;
            case ModulatorTarget::LateralOffset:
                res.lateralOffset += outVal;
                break;
            case ModulatorTarget::WorldOffsetX:
                res.worldOffsetX += outVal;
                break;
            case ModulatorTarget::WorldOffsetY:
                res.worldOffsetY += outVal;
                break;
            case ModulatorTarget::ParticleLength:
                res.particleLength = std::max(0.05f, res.particleLength * outVal);
                break;
            case ModulatorTarget::HueShiftDeg:
                res.hueShiftDeg += outVal;
                break;
            case ModulatorTarget::LightnessShift:
                res.lightnessShift = std::clamp(res.lightnessShift + outVal, -1.0f, 1.0f);
                break;
            case ModulatorTarget::OpacityScale:
                res.opacityScale = std::clamp(res.opacityScale * outVal, 0.0f, 1.0f);
                break;
        }
    }
    return res;
}

/**
 * @brief Computes the final RGBA color of a coat accounting for color modes and modulation.
 *
 * Handles:
 * - InheritBase: Inherits stroke base color.
 * - FixedColor: Overrides with explicit coat color.
 * - InvertBase: Automatically applies complementary 180-degree hue inversion.
 * - Dynamic Hue & Lightness Shifts: Converts to HSL, applies offsets, clamps, and converts back.
 *
 * @param coat Coat specification owning color rules.
 * @param baseColor Inking stroke root color.
 * @param hueShift Delta hue in degrees to shift.
 * @param lightnessShift Delta lightness [-1.0, +1.0] to shift.
 * @param[out] outAlpha Extracted normalized alpha channel [0.0, 1.0].
 * @return Final evaluated RGBA color.
 */
ColorRgba ModulatorEvaluator::ComputeCoatColor(
    const BrushCoatSpec& coat,
    ColorRgba baseColor,
    float hueShift,
    float lightnessShift,
    float& outAlpha) noexcept {

    ColorRgba c = baseColor;
    if (coat.colorMode == CoatColorMode::FixedColor) {
        c = coat.fixedColor;
    } else if (coat.colorMode == CoatColorMode::InvertBase) {
        hueShift += 180.0f;
    }

    // Apply color modifications in HSL space if any shifts are requested
    if (std::abs(hueShift) > 0.1f || std::abs(lightnessShift) > 0.01f) {
        float h = 0.0f, s = 0.0f, l = 0.0f;
        RgbToHsl(c, h, s, l);
        h = std::fmod(h + hueShift, 360.0f);
        if (h < 0.0f) h += 360.0f;
        l = std::clamp(l + lightnessShift, 0.0f, 1.0f);
        c = HslToRgb(h, s, l, c.a);
    }

    outAlpha = (static_cast<float>(c.a) / 255.0f);
    return c;
}

/**
 * @brief Extracts the raw physical telemetry scalar for a given ModulatorSource.
 *
 * Acts as the hardware/modeling adapter feeding signals into the modulation pipeline.
 *
 * @param src The telemetry channel to query (pressure, tilt, distance, speed, time, etc.).
 * @param ctx Snapshot of instantaneous kinematics and timestamps.
 * @return Raw floating-point signal magnitude.
 */
float ModulatorEvaluator::GetSourceValue(ModulatorSource src, const PointContext& ctx) noexcept {
    switch (src) {
        case ModulatorSource::ArcLength:          return ctx.arcLength;
        case ModulatorSource::DistanceRemaining:  return ctx.distanceRemaining;
        case ModulatorSource::Pressure:           return ctx.pressure;
        case ModulatorSource::PressureIntegral:   return ctx.pressureIntegral;
        case ModulatorSource::Velocity:           return ctx.velocity;
        case ModulatorSource::Tilt:               return ctx.tilt;
        case ModulatorSource::TravelAngle:        return ctx.travelAngle;
        case ModulatorSource::StrokeProgress:     return ctx.progress;
        case ModulatorSource::ParticleIndex:      return ctx.particleIndex;
        case ModulatorSource::TimeSeconds:        return ctx.timeSeconds;
        case ModulatorSource::TimeSinceInput:     return ctx.timeSinceInput;
        case ModulatorSource::TimeSinceStrokeEnd: return ctx.timeSinceStrokeEnd;
    }
    return 0.0f;
}

/**
 * @brief Evaluates transfer function / waveform mapping over normalized input x.
 *
 * Supported transfer curves:
 * - Linear: Direct proportionality (used for pressure-to-width, tilt-to-width, etc.).
 * - SineWave: Continuous sinusoidal oscillation (used for ribbon weaving, calligraphic chisel).
 * - TriangleZigzag: Linear symmetric oscillation between -1.0 and +1.0.
 * - MirrorCycle: Sawtooth ping-pong cycle [0 -> 1 -> 0] (used for smooth graffiti hue shifting).
 * - AlternateSwap: Square wave switching +/-1.0 (used for alternating holiday light bulbs).
 * - ValueNoise1D: Continuous organic value noise along stroke travel.
 * - HashRandom: Discrete per-particle white noise in [0.0, 1.0] (used for pencil specks).
 * - EaseOutClamped: Cubic ease-out deceleration curve over [0.0, 1.0].
 * - EaseOutDripSynced: Dynamic fluid drips (Google wet_paint) clustered by spatial noise.
 * - BlinkPulseClamped: Multi-flash bulb twinkle animation (Google holiday_lights).
 * - TravelingWaveJelly: Spatial traveling sine wave damped on stroke lift (Google jelly_wobble).
 *
 * @param wf Waveform or transfer curve function enum.
 * @param x Phase-adjusted radian angle (effectiveFrequency * source * 2PI + phase).
 * @param rawLinearArg Unwrapped normalized argument (effectiveFrequency * source).
 * @param ctx Point context snapshot for time- and space-dependent dynamics.
 * @param seed Pseudo-random seed for stochastic waveforms.
 * @return Evaluated scalar wave output.
 */
float ModulatorEvaluator::EvaluateWaveform(
    ModulatorWaveform wf,
    float x,
    float rawLinearArg,
    const PointContext& ctx,
    uint32_t seed) noexcept {

    switch (wf) {
        case ModulatorWaveform::Linear:
            return rawLinearArg;

        case ModulatorWaveform::SineWave:
            return std::sin(x);

        case ModulatorWaveform::TriangleZigzag: {
            float norm = std::fmod(x / kTwoPi, 1.0f);
            if (norm < 0.0f) norm += 1.0f;
            return (norm < 0.5f) ? (4.0f * norm - 1.0f) : (3.0f - 4.0f * norm);
        }

        case ModulatorWaveform::MirrorCycle: {
            float frac = std::fmod(x / kTwoPi, 2.0f);
            if (frac < 0.0f) frac += 2.0f;
            return (frac <= 1.0f) ? frac : (2.0f - frac);
        }

        case ModulatorWaveform::AlternateSwap: {
            float norm = std::fmod(x / kTwoPi, 1.0f);
            if (norm < 0.0f) norm += 1.0f;
            return (norm < 0.5f) ? 1.0f : -1.0f;
        }

        case ModulatorWaveform::ValueNoise1D:
            return Noise1D(x / kTwoPi, seed);

        case ModulatorWaveform::HashRandom:
            return Hash1D(static_cast<uint32_t>(std::abs(rawLinearArg * 100.0f)), seed);

        case ModulatorWaveform::EaseOutClamped: {
            float u = std::clamp(rawLinearArg, 0.0f, 1.0f);
            float inv = 1.0f - u;
            return 1.0f - inv * inv * inv; // Cubic ease-out, stops at u=1.0
        }

        case ModulatorWaveform::EaseOutDripSynced: {
            // Google wet_paint: synchronized (EaseOut(timeSinceInput / 1.0s) * dripNoisePeak(arcLength, seed))
            float u = std::clamp(rawLinearArg, 0.0f, 1.0f);
            float inv = 1.0f - u;
            float easeOut = 1.0f - inv * inv * inv;

            // Spatial drip clustering along stroke (only distinct peaks become hanging drops)
            float bw = std::max(ctx.baseWidth, 1.0f);
            float noiseCoord = ctx.arcLength / (bw * 1.45f);
            float rawNoise = Noise1D(noiseCoord, seed) * 0.5f + 0.5f; // [0, 1]
            float peak = std::max(0.0f, (rawNoise - 0.36f) / 0.64f);
            return easeOut * (peak * peak);
        }

        case ModulatorWaveform::BlinkPulseClamped: {
            // Google holiday_lights: bulb flashes over [0, 0.60s] when born, plus live multi-bulb twinkle while drawing
            float u = rawLinearArg; // timeSinceInput / 0.60s
            if (u < 1.0f && u >= 0.0f) {
                // Initial double flash as bulb is drawn (matches Google's 5-point piecewise linear flash curve)
                float flash = std::sin(u * kPi * 3.0f);
                return std::max(0.0f, flash);
            }
            // While stroke is still actively being drawn (timeSinceStrokeEnd == 0), bulbs twinkle at staggered phases
            if (ctx.timeSinceStrokeEnd <= 0.0f) {
                float twinkle = std::sin(ctx.timeSinceInput * 5.0f + ctx.particleIndex * 2.1f);
                return (twinkle > 0.25f) ? 0.85f : 0.0f;
            }
            return 0.0f;
        }

        case ModulatorWaveform::TravelingWaveJelly: {
            // Google jelly_wobble: smooth width undulation along stroke over time + EaseOut settling over 0.90s after pen lift
            float bw = std::max(ctx.baseWidth, 1.0f);
            float distCycles = ctx.arcLength / (bw * 3.8f);
            float activePhaseTime = ctx.timeSinceInput;
            if (ctx.timeSinceStrokeEnd > 0.0f) {
                float uEnd = std::clamp(ctx.timeSinceStrokeEnd / 0.90f, 0.0f, 1.0f);
                float inv = 1.0f - uEnd;
                float easeEnd = 1.0f - inv * inv * inv;
                float ageAtPenUp = std::max(0.0f, ctx.timeSinceInput - ctx.timeSinceStrokeEnd);
                activePhaseTime = ageAtPenUp + easeEnd * 0.90f;
            }
            return std::sin((distCycles - activePhaseTime * 2.0f) * kTwoPi);
        }
    }
    return 0.0f;
}

} // namespace Folio
