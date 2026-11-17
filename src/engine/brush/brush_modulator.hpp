#pragma once
/**
 * =========================================================================================
 * @file src/engine/brush/brush_modulator.hpp
 * @brief Signal Modulators, Transfer Functions, and Waveform Evaluators for Brush Recipes
 * =========================================================================================
 */

#include <cstdint>
#include "engine/brush/brush_recipe.hpp"

namespace Folio {

/**
 * @struct PointContext
 * @brief Kinematic, temporal, and spatial snapshot for a single stroke vertex/particle.
 */
struct PointContext {
    float arcLength          = 0.0f; // Distance from stroke start (mm)
    float distanceRemaining = 0.0f; // Distance to stroke end (mm)
    float pressure           = 0.0f; // Stylus contact pressure [0.0, 1.0]
    float pressureIntegral   = 0.0f; // Accumulated pressure over time/distance
    float velocity           = 0.0f; // Instantaneous velocity (mm/s)
    float tilt               = 0.0f; // Stylus tilt magnitude [0.0, 1.0]
    float travelAngle        = 0.0f; // Heading angle in radians
    float progress           = 0.0f; // Normalized stroke progression [0.0, 1.0]
    float particleIndex      = 0.0f; // Particle / dab ordinal index
    float timeSeconds        = 0.0f; // Current monotonic timestamp (s)
    float timeSinceInput     = 0.0f; // Elapsed time since last input sample (s)
    float timeSinceStrokeEnd = 0.0f; // Elapsed time since pointer lift (s)
    float baseWidth          = 0.5f; // Brush base width (mm)
};

/**
 * @struct EvaluatedMods
 * @brief Aggregated output of all modulators evaluated on a single coat.
 */
struct EvaluatedMods {
    float radiusScale    = 1.0f;
    float lateralOffset  = 0.0f;
    float worldOffsetX   = 0.0f;
    float worldOffsetY   = 0.0f;
    float particleLength = 1.0f;
    float hueShiftDeg    = 0.0f;
    float lightnessShift = 0.0f;
    float opacityScale   = 1.0f;
};

class ModulatorEvaluator {
public:
    /**
     * @brief Extracts raw scalar value from the requested input telemetry source.
     */
    [[nodiscard]] static float GetSourceValue(ModulatorSource src, const PointContext& ctx) noexcept;

    /**
     * @brief Evaluates transfer function / waveform mapping over normalized input x.
     */
    [[nodiscard]] static float EvaluateWaveform(
        ModulatorWaveform wf,
        float x,
        float rawLinearArg,
        const PointContext& ctx,
        uint32_t seed) noexcept;

    /**
     * @brief Evaluates all modulators attached to a coat for a given point context.
     */
    [[nodiscard]] static EvaluatedMods EvaluateCoatModulators(
        const BrushCoatSpec& coat,
        const PointContext& ctx,
        float baseWidth) noexcept;

    /**
     * @brief Evaluates final RGBA color with coat color mode, hue shifts, and lightness shifts.
     */
    [[nodiscard]] static ColorRgba ComputeCoatColor(
        const BrushCoatSpec& coat,
        ColorRgba baseColor,
        float hueShift,
        float lightnessShift,
        float& outAlpha) noexcept;

    static void RgbToHsl(ColorRgba rgb, float& hDeg, float& s, float& l) noexcept;
    static ColorRgba HslToRgb(float hDeg, float s, float l, uint8_t a = 255) noexcept;
    static float Noise1D(float x, uint32_t seed) noexcept;
    static float Hash1D(uint32_t index, uint32_t seed) noexcept;
};

} // namespace Folio

