#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/engine/stroke_width_evaluator.hpp
 * @brief Dynamic Stylus Width, Pressure Curve, Velocity Thinning, and Corner Evaluator
 * =========================================================================================
 *
 * Encapsulates all non-linear stylus dynamics:
 * 1. Hardware pressure curve calibration (deadzone, saturation, power-law gamma).
 * 2. Inertial velocity filtering bounded by physical acceleration/deceleration limits.
 * 3. Corner turn-rate compensation to suppress vertex bloating.
 * 4. Velocity thinning (Fountain pen hydrodynamic thinning effect).
 * 5. Spatial cone-slope radius clamping (dr/ds).
 * 6. Exponential Moving Average (EMA) width filter for flicker-free transitions.
 */

#include <cmath>
#include <algorithm>
#include <numbers>
#include "folio_sketch/core/ink_config.hpp"
#include "folio_sketch/core/ink_stroke_data.hpp"

namespace Folio {

class StrokeWidthEvaluator {
public:
    // =========================================================================
    // 0. ANISOTROPIC (ELLIPTICAL) NIB EVALUATOR
    // =========================================================================

    /**
     * @brief Width multiplier of an elliptical nib for a given direction of travel.
     *
     * The nib has a major semi-axis along `nibAngleRad` and a minor semi-axis of
     * `aspect * major`. The ink swath is the nib's extent perpendicular to travel, which for
     * θ = travelHeading - nibAngle evaluates to  sqrt(sin²θ + aspect²·cos²θ).
     * Travelling along the major axis gives the thin hairline (`aspect`); travelling across it
     * gives the full width (1.0). aspect >= 1 returns 1 (round nib).
     */
    [[nodiscard]] static float NibWidthFactor(float travelHeadingRad, float nibAngleRad, float aspect) noexcept {
        if (aspect >= 0.999f) return 1.0f;
        const float a = aspect < 0.02f ? 0.02f : aspect;
        const float th = travelHeadingRad - nibAngleRad;
        const float s = std::sin(th);
        const float c = std::cos(th);
        return std::sqrt(s * s + a * a * c * c);
    }

    // =========================================================================
    // 1. INERTIAL ACCELERATION & CORNER COMPENSATION EVALUATORS
    // =========================================================================

    /**
     * @brief Filters raw input speed through asymmetric physical acceleration and deceleration limits.
     * @param rawSpeed Instantaneous speed (mm/s).
     * @param previousFilteredSpeed Previous filtered speed (mm/s).
     * @param dt Elapsed time interval (seconds).
     * @param maxAccel Maximum positive acceleration (mm/s^2).
     * @param maxDecel Maximum deceleration limit (mm/s^2).
     * @return Inertially clamped speed in mm/s (>= 0.0f).
     */
    [[nodiscard]] static float FilterSpeedWithAcceleration(float rawSpeed,
                                                           float previousFilteredSpeed,
                                                           float dt,
                                                           float maxAccel,
                                                           float maxDecel) noexcept {
        if (dt <= 1e-5f) {
            return previousFilteredSpeed;
        }

        float deltaV = rawSpeed - previousFilteredSpeed;
        float minDeltaV = -maxDecel * dt;
        float maxDeltaV = maxAccel * dt;
        deltaV = std::clamp(deltaV, minDeltaV, maxDeltaV);

        return std::max(0.0f, previousFilteredSpeed + deltaV);
    }

    /**
     * @brief Computes velocity thinning factor with turn-rate speed compensation.
     *
     * When turning sharp corners (turnAngleRad > cornerCompensationThresholdRad), pen travel speed
     * drops toward zero, which would normally collapse velocity thinning to 1.0 (causing an unnatural
     * corner bulge). This method blends speed toward an effective cruising speed (0.4 * maxSpeed)
     * proportional to turn sharpness before computing thinning.
     *
     * @param filteredSpeedMmPerSec Inertially filtered pen speed (mm/s).
     * @param turnAngleRad Angular turn delta between consecutive segments in radians [0, pi].
     * @param zoomScale Viewport zoom scale.
     * @param config Active inking configuration.
     * @return Hydrodynamic velocity thinning factor in [minWidthMultiplier, 1.0].
     */
    [[nodiscard]] static float ComputeCompensatedVelocityThinning(float filteredSpeedMmPerSec,
                                                                 float turnAngleRad,
                                                                 float zoomScale,
                                                                 const InkingConfig& config) noexcept {
        if (!config.dynamics.useVelocity) {
            return 1.0f;
        }

        float effectiveSpeed = filteredSpeedMmPerSec;

        if (config.dynamics.useCornerCompensation &&
            turnAngleRad > config.dynamics.cornerCompensationThresholdRad) {
            float maxSpeed = std::max(1.0f, config.dynamics.velocityThinningMaxSpeed);
            float cruisingSpeed = 0.4f * maxSpeed;

            // Blend toward cruisingSpeed proportional to turn sharpness past threshold up to pi
            constexpr float kPi = 3.14159265358979323846f;
            float denom = std::max(1e-4f, kPi - config.dynamics.cornerCompensationThresholdRad);
            float t = std::clamp((turnAngleRad - config.dynamics.cornerCompensationThresholdRad) / denom, 0.0f, 1.0f);
            // Smoothstep blending
            float smoothT = t * t * (3.0f - 2.0f * t);
            effectiveSpeed = std::max(effectiveSpeed, std::lerp(effectiveSpeed, cruisingSpeed, smoothT));
        }

        return ComputeVelocityThinning(effectiveSpeed, zoomScale, config);
    }

    /**
     * @brief Clamps spatial cone-slope (dr/ds) to prevent radius expansion from outpacing arc length.
     * @param targetRadius Calculated unconstrained target radius (mm).
     * @param previousRadius Radius of previous modeled point (mm).
     * @param segmentLengthMm Arc length between previous point and current point (mm).
     * @param maxSlope Maximum allowable dr/ds slope ratio.
     * @return Spatially bounded radius (mm).
     */
    [[nodiscard]] static float ClampSpatialRadiusSlope(float targetRadius,
                                                      float previousRadius,
                                                      float segmentLengthMm,
                                                      float maxSlope) noexcept {
        if (previousRadius <= 0.0f || segmentLengthMm <= 0.0f || maxSlope <= 0.0f) {
            return targetRadius;
        }

        float maxDeltaR = segmentLengthMm * maxSlope;
        return std::clamp(targetRadius, previousRadius - maxDeltaR, previousRadius + maxDeltaR);
    }

    // =========================================================================
    // 2. DYNAMIC WIDTH CALCULATION ENTRY POINTS
    // =========================================================================

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
    [[nodiscard]] static float CalculateDynamicWidthWithTiltRad(float baseWidth,
                                                                float pressure,
                                                                float speed,
                                                                float tiltRad,
                                                                float previousWidth,
                                                                float zoomScale,
                                                                const InkingConfig& config,
                                                                bool isPencil = false,
                                                                float turnAngleRad = 0.0f) noexcept {
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
        float rawVelocityFactor = (config.dynamics.useCornerCompensation && turnAngleRad > 0.0f)
                                   ? ComputeCompensatedVelocityThinning(effSpeed, turnAngleRad, zoomScale, config)
                                   : ComputeVelocityThinning(effSpeed, zoomScale, config);

        float smoothedVelocityFactor = rawVelocityFactor;
        if (previousWidth > 0.0f && pressureWidth > 0.0f) {
            float previousVelFactor = std::clamp(previousWidth / pressureWidth, 0.01f, 1.0f);
            smoothedVelocityFactor = ApplySmoothingEMA(previousVelFactor, rawVelocityFactor, config.dynamics.velocitySmoothingEma);
        }

        float tiltFactor = ComputeTiltModulation(tiltRad, config);
        float targetWidth = pressureWidth * smoothedVelocityFactor * tiltFactor;
        return ApplySmoothingEMA(previousWidth, targetWidth, config.dynamics.widthSmoothingEma);
    }

    /**
     * @brief Computes dynamic stroke width from explicit telemetry values.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth, float pressure,
                                                     float speed, float tiltX, float tiltY,
                                                     float previousWidth, float zoomScale,
                                                     const InkingConfig& config,
                                                     bool isPencil = false,
                                                     float turnAngleRad = 0.0f) noexcept {
        float tiltRad = (config.dynamics.useTilt && (tiltX != 0.0f || tiltY != 0.0f)) ? std::hypot(tiltX, tiltY) : 0.0f;
        return CalculateDynamicWidthWithTiltRad(baseWidth, pressure, speed, tiltRad,
                                                previousWidth, zoomScale, config, isPencil, turnAngleRad);
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

    // =========================================================================
    // 3. INTERNAL EVALUATORS
    // =========================================================================
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
        float normalizedSpeed = std::clamp(speed / maxSpeed, 0.0f, 1.0f);
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
