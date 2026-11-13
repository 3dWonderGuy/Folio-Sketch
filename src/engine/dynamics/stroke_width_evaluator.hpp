#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/engine/dynamics/stroke_width_evaluator.hpp
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
#include "core/ink_config.hpp"
#include "core/ink_stroke_data.hpp"

namespace Folio {

class StrokeWidthEvaluator {
public:
    // =========================================================================
    // 0. DIRECTIONAL / ANISOTROPIC NIB EVALUATOR
    // =========================================================================

    /**
     * @brief Computes directional thinning factor based on travel heading relative to a chisel/elliptical nib angle.
     *
     * The nib has a major semi-axis along `nibAngleRad` and a minor semi-axis of `aspect * major`.
     * The effective swath is the nib's extent perpendicular to travel:
     *   θ = travelHeading - nibAngle evaluates to sqrt(sin²θ + aspect²·cos²θ).
     *
     * - Travelling in line with the blade/major axis yields maximum directional thinning (`aspect`).
     * - Travelling perpendicular across the blade yields full width (`1.0`).
     * - aspect >= 1.0 indicates an isotropic round nib (returns 1.0, no thinning).
     *
     * @param travelHeadingRad Instantaneous direction of stroke motion in radians.
     * @param nibAngleRad Fixed orientation angle of the chisel blade in radians.
     * @param aspect Ratio of minor-to-major axis (e.g. 0.1 for sharp calligraphy, 1.0 for round).
     * @return Directional width multiplier in [aspect, 1.0].
     */
    [[nodiscard]] static float DirectionThinner(float travelHeadingRad, float nibAngleRad, float aspect) noexcept {
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

    // Stage one, clean up raw speed with VelocityNoiseFilter()
    // Stage two, evaluate dynamic width modulation with InkFlowSimulator()
    // Stage three, prevent radius runaway with ClampSpatialRadiusSlope()

    /**
     * @brief Filters raw input speed through an inertial slew rate clamper.
     * @param rawSpeed Speed (mm/s).
     * @param previousFilteredSpeed Previous filtered speed (mm/s).
     * @param dt Elapsed time interval (seconds).
     * @param maxAccel Maximum positive acceleration (mm/s^2).
     * @param maxDecel Maximum deceleration limit (mm/s^2).
     * @return Inertially clamped speed in mm/s (>= 0.0f).
     */
    [[nodiscard]] static float VelocityNoiseFilter(float rawSpeed,
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
     * @brief Evaluates dynamic width modulation from motion velocity, corner turn dynamics, and optional dwell time.
     *
     * Pipeline Steps:
     * 1. Corner turn compensation: Prevents momentary direction reversal slowdowns from bloating vertices.
     * 2. Hydrodynamic speed thinning: Models fountain pen ink starvation at high travel speeds.
     * 3. Time dwell accumulation: If timeDwellMultiplier > 0, dwell time increases flow (Mode B: spray/bleed).
     *                             If timeDwellMultiplier == 0 (Mode A: note-taking), time has zero effect.
     *
     * @param filteredSpeedMmPerSec Inertially filtered pen speed (mm/s).
     * @param turnAngleRad Angular turn delta between consecutive segments in radians [0, pi].
     * @param zoomScale Viewport zoom scale.
     * @param config Active inking configuration.
     * @param dt Elapsed time interval (seconds) since last sample.
     * @return Dynamic width modulation multiplier.
     */
    [[nodiscard]] static float InkFlowSimulator(float filteredSpeedMmPerSec,
                                                           float turnAngleRad,
                                                           float zoomScale,
                                                           const InkingConfig& config,
                                                           float dt = 0.0f) noexcept {
        if (!config.dynamics.useVelocity && config.dynamics.timeDwellMultiplier <= 0.0f) {
            return 1.0f;
        }

        float effectiveSpeed = filteredSpeedMmPerSec;

        // 1. Corner turn-rate compensation (prevents false slowdowns from bloating sharp corners)
        if (config.dynamics.useCornerCompensation &&
            turnAngleRad > config.dynamics.cornerCompensationThresholdRad) {
            float maxSpeed = std::max(1.0f, config.dynamics.velocityThinningMaxSpeed);
            float cruisingSpeed = 0.4f * maxSpeed;

            // Blend toward cruisingSpeed proportional to turn sharpness past threshold up to pi
            constexpr float kPi = std::numbers::pi_v<float>;
            float denom = std::max(1e-4f, kPi - config.dynamics.cornerCompensationThresholdRad);
            float t = std::clamp((turnAngleRad - config.dynamics.cornerCompensationThresholdRad) / denom, 0.0f, 1.0f);
            // Smoothstep blending
            float smoothT = t * t * (3.0f - 2.0f * t);
            effectiveSpeed = std::max(effectiveSpeed, std::lerp(effectiveSpeed, cruisingSpeed, smoothT));
        }

        // 2. Base hydrodynamic speed factor (fountain pen starvation thinning at high speeds)
        float modFactor = ComputeVelocityThinning(effectiveSpeed, zoomScale, config);

        // 3. Optional Time-Dwell Accumulator (Mode A vs Mode B)
        // - timeDwellMultiplier == 0.0f (Mode A: Note-Taking): Time has zero effect (stationary locked).
        // - timeDwellMultiplier  > 0.0f (Mode B: Spray/Bleed): Low-speed dwell accumulates ink over dt.
        if (config.dynamics.timeDwellMultiplier > 0.0f && dt > 0.0f) {
            float maxSpeed = std::max(1.0f, config.dynamics.velocityThinningMaxSpeed);
            float stationaryWeight = 1.0f - std::clamp(effectiveSpeed / maxSpeed, 0.0f, 1.0f);
            float dwellGrowth = config.dynamics.timeDwellMultiplier * dt * stationaryWeight;
            modFactor += dwellGrowth;
        }

        return modFactor;
    }

    /**
     * @brief Clamps spatial cone-slope (dr/ds) to prevent radius expansion from outpacing arc length.
     * @param targetRadius Calculated unconstrained target radius (mm).
     * @param previousRadius Radius of previous modeled point (mm).
     * @param segmentLengthMm Arc length between previous point and current point (mm).
     * @param maxSlope Maximum allowable dr/ds slope change.
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
    // 2. DYNAMIC BASELINE WIDTH CALCULATION
    // =========================================================================

    /**
     * @brief Computes dynamic baseline stroke width from calibrated pressure and velocity dynamics.
     *
     * Pipeline Steps:
     * 1. ScaleWidthByPressure(): Modulates tool baseWidth by hardware pressure curve.
     * 2. InkFlowSimulator(): Evaluates speed thinning, corner compensation, and dwell accumulation.
     * 3. Multiplies factors to establish instantaneous target thickness.
     * 4. ApplySmoothingEMA(): Dampens transitions against previous point to eliminate jitter.
     *
     * Higher-order dynamic modulators (tilt shading spread, custom pressure-to-opacity,
     * anisotropic nibs, particles, etc.) are attached independently per-coat by BrushComposer.
     *
     * @param baseWidth Base stroke diameter in world millimeters.
     * @param pressure Hardware stylus pressure [0.0, 1.0]. Negative values indicate non-pressure input.
     * @param speed Instantaneous pen speed (mm/s).
     * @param previousWidth Filtered width of previous point in millimeters (negative if uninitialized).
     * @param zoomScale Viewport zoom scale.
     * @param config Active inking configuration.
     * @param turnAngleRad Angular turn delta between consecutive segments in radians [0, pi].
     * @param dt Elapsed time interval (seconds) since previous sample.
     * @return Dynamically evaluated and smoothed baseline stroke width in millimeters.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth,
                                                     float pressure,
                                                     float speed,
                                                     float previousWidth,
                                                     float zoomScale,
                                                     const InkingConfig& config,
                                                     float turnAngleRad = 0.0f,
                                                     float dt = 0.0f) noexcept {
        float pressureWidth = ScaleWidthByPressure(baseWidth, pressure, config);
        float rawModFactor = InkFlowSimulator(speed, turnAngleRad, zoomScale, config, dt);

        float smoothedModFactor = rawModFactor;
        if (previousWidth > 0.0f && pressureWidth > 0.0f) {
            float previousFactor = std::clamp(previousWidth / pressureWidth, 0.01f, 1.0f);
            smoothedModFactor = ApplySmoothingEMA(previousFactor, rawModFactor, config.dynamics.velocitySmoothingEma);
        }

        float targetWidth = pressureWidth * smoothedModFactor;
        return ApplySmoothingEMA(previousWidth, targetWidth, config.dynamics.widthSmoothingEma);
    }

    /**
     * @brief Computes dynamic baseline stroke width from StylusInputPoint telemetry.
     * @param baseWidth Base stroke diameter in world millimeters.
     * @param pt Stylus input point with hardware telemetry (pressure, speed).
     * @param previousWidth Filtered width of previous point in millimeters (negative if uninitialized).
     * @param zoomScale Viewport zoom scale.
     * @param config Active inking configuration.
     * @param turnAngleRad Angular turn delta between consecutive segments in radians [0, pi].
     * @param dt Elapsed time interval (seconds) since previous sample.
     * @return Dynamically evaluated and smoothed baseline stroke width in millimeters.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth,
                                                     const StylusInputPoint& pt,
                                                     float previousWidth,
                                                     float zoomScale,
                                                     const InkingConfig& config,
                                                     float turnAngleRad = 0.0f,
                                                     float dt = 0.0f) noexcept {
        return CalculateDynamicWidth(baseWidth, pt.pressure, pt.speed,
                                     previousWidth, zoomScale, config, turnAngleRad, dt);
    }

    // =========================================================================
    // 3. MODULAR MATHEMATICAL DYNAMICS PRIMITIVES
    // =========================================================================

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
