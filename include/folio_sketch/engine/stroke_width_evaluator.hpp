#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/stroke_width_evaluator.hpp
 * @brief Dynamic Stylus Width, Pressure Curve, and Velocity Thinning Evaluator
 * =========================================================================================
 *
 * Encapsulates all non-linear stylus dynamics:
 * 1. Hardware pressure curve calibration (deadzone, saturation, power-law gamma).
 * 2. Pressure-to-thickness bounding and scaling.
 * 3. Velocity thinning (Fountain pen hydrodynamic thinning effect).
 * 4. Exponential Moving Average (EMA) width filter for flicker-free transitions.
 */

#include <cmath>
#include <algorithm>
#include "folio_sketch/ink_config.hpp"
#include "folio_sketch/ink_stroke_data.hpp"

namespace Folio {

class StrokeWidthEvaluator {
public:
    /**
     * @brief Evaluates complete dynamic stroke thickness from Point2D telemetry and applies EMA smoothing.
     * 
     * Pipeline Steps:
     * 1. ScaleWidthByPressure(): Modulates tool baseSize by hardware pressure curve.
     * 2. ComputeVelocityThinning(): Applies fountain pen speed thinning factor.
     * 3. ComputeTiltModulation(): Applies stylus tilt factor.
     * 4. Multiplies factors to establish instantaneous target thickness.
     * 5. ApplySmoothingEMA(): Dampens transitions against previous point to eliminate jitter.
     */
    /**
     * @brief Computes dynamic stroke width from explicit telemetry values with pre-resolved tilt radius.
     * @param isPencil If true, uses graphite pencil physics where pressure controls grain/opacity rather
     *                 than ballooning tip width (tilt controls shading spread).
     */
    [[nodiscard]] static float CalculateDynamicWidthWithTiltRad(float baseWidth, float pressure,
                                                                float speed, float tiltRad,
                                                                float previousWidth, float zoomScale,
                                                                const InkingConfig& config,
                                                                bool isPencil = false) noexcept {
        float effBaseWidth = baseWidth;
        float effPressure = pressure;
        float effSpeed = speed;

        if (isPencil) {
            // Graphite pencil lead stays rigid under pressure (only ±12% width variance)
            if (config.dynamics.usePressure && pressure >= 0.0f) {
                effBaseWidth = baseWidth * (0.90f + 0.12f * std::clamp(pressure, 0.0f, 1.0f));
            }
            effPressure = -1.0f; // bypass non-linear pressure gamma scaling
            effSpeed = 0.0f;    // pencil lead does not exhibit fountain pen hydrodynamic velocity thinning
        }

        float pressureWidth = ScaleWidthByPressure(effBaseWidth, effPressure, config);
        float velocityFactor = ComputeVelocityThinning(effSpeed, zoomScale, config);
        float tiltFactor = ComputeTiltModulation(tiltRad, config);
        float targetWidth = pressureWidth * velocityFactor * tiltFactor;
        return ApplySmoothingEMA(previousWidth, targetWidth, config.dynamics.widthSmoothingEma);
    }

    /**
     * @brief Computes dynamic stroke width from explicit telemetry values.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth, float pressure,
                                                     float speed, float tiltX, float tiltY,
                                                     float previousWidth, float zoomScale,
                                                     const InkingConfig& config,
                                                     bool isPencil = false) noexcept {
        float tiltRad = (config.dynamics.useTilt && (tiltX != 0.0f || tiltY != 0.0f)) ? std::hypot(tiltX, tiltY) : 0.0f;
        return CalculateDynamicWidthWithTiltRad(baseWidth, pressure, speed, tiltRad,
                                                previousWidth, zoomScale, config, isPencil);
    }

    /**
     * @brief Computes dynamic stroke width from StylusInputPoint telemetry.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth, const StylusInputPoint& pt,
                                                     float previousWidth, float zoomScale,
                                                     const InkingConfig& config) noexcept {
        return CalculateDynamicWidth(baseWidth, pt.pressure, pt.speed, pt.tiltX, pt.tiltY,
                                     previousWidth, zoomScale, config);
    }

private:
    /**
     * @brief Maps raw hardware stylus pressure through deadzone, saturation, and power-law gamma curve.
     * @param rawPressure Hardware stylus pressure reported by digitizer [0.0, 1.0].
     * @param config Active inking configuration.
     * @return Normalized response factor in [0.0, 1.0].
     */
    [[nodiscard]] static float ApplyPressureCurve(float rawPressure, const InkingConfig& config) noexcept {
        if (!config.calibration.enablePressureCurve) {
            return std::isnan(rawPressure) ? 0.5f : std::clamp(rawPressure, 0.0f, 1.0f);
        }
        if (std::isnan(rawPressure) || rawPressure <= config.calibration.pressureDeadzoneMin) {
            return 0.0f;
        }
        if (rawPressure >= config.calibration.pressureSaturationMax) {
            return 1.0f;
        }

        float denom = config.calibration.pressureSaturationMax - config.calibration.pressureDeadzoneMin;
        if (denom <= 1e-6f) {
            return 1.0f;
        }

        float norm = std::clamp((rawPressure - config.calibration.pressureDeadzoneMin) / denom, 0.0f, 1.0f);
        if (config.calibration.pressureGamma <= 0.0f || config.calibration.pressureGamma == 1.0f) {
            return norm;
        }
        return std::clamp(std::pow(norm, config.calibration.pressureGamma), 0.0f, 1.0f);
    }

    /**
     * @brief Scales base tool diameter by calibrated stylus pressure response.
     * @param baseWidth Base tool diameter in world millimeters.
     * @param rawPressure Hardware stylus pressure [0.0, 1.0]. Negative values indicate non-pressure input.
     * @param config Active inking configuration.
     * @return Pressure-scaled stroke width in millimeters.
     */
    [[nodiscard]] static float ScaleWidthByPressure(float baseWidth, float rawPressure,
                                                    const InkingConfig& config) noexcept {
        if (!config.dynamics.usePressure || rawPressure < 0.0f) {
            return baseWidth;
        }

        float pressureCurved = ApplyPressureCurve(rawPressure, config);
        float minFrac = std::min(config.dynamics.minWidthMultiplier, config.dynamics.maxWidthMultiplier);
        float maxFrac = std::max(config.dynamics.minWidthMultiplier, config.dynamics.maxWidthMultiplier);
        float widthMultiplier = minFrac + pressureCurved * (maxFrac - minFrac);
        return baseWidth * std::clamp(widthMultiplier, minFrac, maxFrac);
    }

    /**
     * @brief Computes velocity thinning multiplier (hydrodynamic fountain pen simulation).
     * @param speedMmPerSec Euclidean pen speed in millimeters per second.
     * @param zoomScale Active canvas viewport zoom scale.
     * @param config Active inking configuration.
     * @return Thinning factor in [config.dynamics.minWidthMultiplier, 1.0].
     */
    [[nodiscard]] static float ComputeVelocityThinning(float speedMmPerSec, float zoomScale,
                                                       const InkingConfig& config) noexcept {
        if (!config.dynamics.useVelocity || speedMmPerSec <= 0.0f) {
            return 1.0f;
        }

        float speed = speedMmPerSec * zoomScale;
        float maxSpeed = std::max(1.0f, config.dynamics.velocityThinningMaxSpeed);
        float normalizedSpeed = std::clamp(
            speed / maxSpeed, 0.0f, 1.0f);
        float minThin = std::clamp(config.dynamics.minWidthMultiplier, 0.01f, 1.0f);
        return 1.0f - normalizedSpeed * (1.0f - minThin);
    }

    /**
     * @brief Computes tilt modulation factor from pre-resolved tilt radius.
     * @param tiltRad Stylus tilt magnitude in radians from normal.
     * @param config Active inking configuration.
     * @return Multiplier factor for stroke width (1.0 = no tilt alteration).
     */
    [[nodiscard]] static float ComputeTiltModulation(float tiltRad,
                                                     const InkingConfig& config) noexcept {
        if (!config.dynamics.useTilt || tiltRad <= config.dynamics.tiltDeadzoneRad) {
            return 1.0f;
        }
        if (tiltRad >= config.dynamics.tiltMaxAngleRad) {
            return config.dynamics.maxTiltWidthMultiplier;
        }

        float range = config.dynamics.tiltMaxAngleRad - config.dynamics.tiltDeadzoneRad;
        if (range <= 1e-5f) {
            return config.dynamics.maxTiltWidthMultiplier;
        }

        float t = std::clamp((tiltRad - config.dynamics.tiltDeadzoneRad) / range, 0.0f, 1.0f);
        // Smoothstep interpolation (Hermite polynomial) for natural shading onset
        float smoothT = t * t * (3.0f - 2.0f * t);
        return 1.0f + smoothT * (config.dynamics.maxTiltWidthMultiplier - 1.0f);
    }

    /**
     * @brief Computes tilt modulation factor (e.g. pencil shading / chisel flat angle).
     * @param tiltX Stylus tilt angle along X axis in radians.
     * @param tiltY Stylus tilt angle along Y axis in radians.
     * @param config Active inking configuration.
     * @return Multiplier factor for stroke width (1.0 = no tilt alteration).
     */
    [[nodiscard]] static float ComputeTiltModulation(float tiltX, float tiltY,
                                                     const InkingConfig& config) noexcept {
        if (!config.dynamics.useTilt || (tiltX == 0.0f && tiltY == 0.0f)) {
            return 1.0f;
        }
        return ComputeTiltModulation(std::hypot(tiltX, tiltY), config);
    }

    /**
     * @brief Applies an Exponential Moving Average (EMA) filter to prevent abrupt thickness jumps.
     * @param currentSmoothed Current smoothed width in millimeters (negative if uninitialized).
     * @param targetWidth Instantaneous target width in millimeters.
     * @param smoothingFactor EMA weight factor in [0.0, 1.0] (0 = sluggish, 1 = instant).
     * @return Filtered stroke width in millimeters.
     */
    [[nodiscard]] static float ApplySmoothingEMA(float currentSmoothed, float targetWidth,
                                                 float smoothingFactor) noexcept {
        if (currentSmoothed < 0.0f) {
            return targetWidth;
        }
        return std::lerp(currentSmoothed, targetWidth, std::clamp(smoothingFactor, 0.0f, 1.0f));
    }
};

} // namespace Folio
