#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/engine/dynamics/stylus_dynamics.hpp
 * @brief Calibrated Stylus Hardware Signal & Dynamics Evaluator
 * =========================================================================================
 *
 * Pure signal-processing layer for raw hardware stylus telemetry:
 * 1. Pressure: Deadzone, saturation, and power-law gamma calibration -> [0.0, 1.0].
 * 2. Velocity: Slew-rate acceleration/deceleration noise filter -> mm/s.
 * 3. Tilt: Altitude & azimuth extraction with deadzone/curve -> [0.0, 1.0] + direction.
 * 4. Spatial Slope: Geometric dr/ds cone-slope limiter to prevent radius runaway.
 *
 * This module is strictly agnostic of visual targets (width, opacity, grain, etc.).
 * Low-latency inking uses these signals directly; high-level brush composition
 * reuses them to drive complex media behaviors.
 */

#include <algorithm>
#include <cmath>
#include <numbers>
#include "core/ink_config.hpp"

namespace Folio {

struct StylusTiltSignal {

    float magnitude = 0.0f;  // Normalized tilt [0.0 = upright, 1.0 = max angle]
    float azimuthRad = 0.0f; // Direction of stylus barrel in radians [-pi, pi]
    float dirX = 0.0f;       // Unit vector X along azimuth
    float dirY = 0.0f;       // Unit vector Y along azimuth
};

class StylusDynamics {
public:

    /**
     * @brief Calibrates raw stylus pressure through deadzone, saturation, and gamma curve.
     * @param rawPressure Hardware pressure [0.0, 1.0]. Negative values indicate non-pressure input.
     * @param config Active inking configuration.
     * @return Normalized calibrated pressure in [0.0, 1.0].
     */
    [[nodiscard]] static float EvaluatePressure(float rawPressure, const InkingConfig& config) noexcept {

        // check if it is enabled
        if (!config.calibration.enablePressureCurve) {
            return std::isnan(rawPressure) ? 0.5f : std::clamp(rawPressure, 0.0f, 1.0f);
        }
        // check if it is in deadzone
        if (std::isnan(rawPressure) || rawPressure <= config.calibration.pressureDeadzoneMin) {
            return 0.0f;
        }
        // check if it is in saturation
        if (rawPressure >= config.calibration.pressureSaturationMax) {
            return 1.0f;
        }
        // edge case guard: avoid divide by zero and clamp to [0,1]
        float denom = config.calibration.pressureSaturationMax - config.calibration.pressureDeadzoneMin;
        if (denom <= 1e-6f) {
            return 1.0f;
        }
        // gamma curve safety check
        float norm = std::clamp((rawPressure - config.calibration.pressureDeadzoneMin) / denom, 0.0f, 1.0f);
        if (config.calibration.pressureGamma <= 0.0f || config.calibration.pressureGamma == 1.0f) {
            return norm;
        }

        // apply final curve mapping
        return std::clamp(std::pow(norm, config.calibration.pressureGamma), 0.0f, 1.0f);
    }

    /**
     * @brief Filters raw pen speed through an inertial slew-rate clamper to eliminate dt jitter spikes.
     * @param rawSpeed Raw Euclidean speed (mm/s).
     * @param previousFilteredSpeed Previous filtered speed (mm/s).
     * @param dt Elapsed time interval (seconds).
     * @param maxAccel Maximum positive acceleration (mm/s^2).
     * @param maxDecel Maximum deceleration limit (mm/s^2).
     * @return Inertially clamped speed in mm/s (>= 0.0f).
     */
    [[nodiscard]] static float FilterVelocity(float rawSpeed,
                                              float previousFilteredSpeed,
                                              float dt,
                                              float maxAccel,
                                              float maxDecel) noexcept {

        // safety check: avoid divide by zero                                        
        if (dt <= 1e-5f) return previousFilteredSpeed;

        //calculate chane in speed
        float deltaV = rawSpeed - previousFilteredSpeed;

        // clamp acceleration/deceleration to prevent jitter spikes
        deltaV = std::clamp(deltaV, -maxDecel * dt, maxAccel * dt);
        
        // return the new filtered speed
        return std::max(0.0f, previousFilteredSpeed + deltaV);
    }

    /**
     * @brief Extracts calibrated tilt magnitude and barrel azimuth from raw tiltX/tiltY angles.
     * @param tiltX Stylus tilt angle along X axis in radians.
     * @param tiltY Stylus tilt angle along Y axis in radians.
     * @param config Active inking configuration.
     * @return StylusTiltSignal with normalized magnitude [0.0, 1.0] and unit direction vector.
     */
    [[nodiscard]] static StylusTiltSignal EvaluateTilt(float tiltX,
                                                       float tiltY,
                                                       const InkingConfig& config) noexcept {

        StylusTiltSignal signal;
        // check if tilt modulation is enabled and if the stylus is tilted
        if (!config.dynamics.useTilt || (tiltX == 0.0f && tiltY == 0.0f)) {
            return signal;
        }
        // compute polar tilt magnitude and azimuth
        float tiltRad = std::hypot(tiltX, tiltY);
        if (tiltRad <= config.dynamics.tiltDeadzoneRad) {
            return signal;
        }

        // compute azimuth and unit direction vector
        signal.azimuthRad = std::atan2(tiltY, tiltX);
        signal.dirX = tiltX / tiltRad;
        signal.dirY = tiltY / tiltRad;

        // apply deadzone and smoothstep curve to magnitude
        if (tiltRad >= config.dynamics.tiltMaxAngleRad) {
            signal.magnitude = 1.0f;
            return signal;
        }

        // compute normalized tilt magnitude in [0.0, 1.0] with smoothstep onset
        float range = config.dynamics.tiltMaxAngleRad - config.dynamics.tiltDeadzoneRad;
        if (range <= 1e-5f) {
            signal.magnitude = 1.0f;
            return signal;
        }
        
        // smoothstep onset for natural shading transition
        float t = std::clamp((tiltRad - config.dynamics.tiltDeadzoneRad) / range, 0.0f, 1.0f);
        // Smoothstep onset for natural shading transition
        signal.magnitude = t * t * (3.0f - 2.0f * t);
        return signal;
    }

    /**
     * @brief Computes velocity thinning multiplier (hydrodynamic fountain pen simulation).
     * @param speedMmPerSec Pen speed in mm/s.
     * @param zoomScale Viewport zoom scale.
     * @param config Active inking configuration.
     * @return Thinning factor in [minWidthMultiplier, 1.0].
     */
    [[nodiscard]] static float ComputeVelocityThinning(float speedMmPerSec,
                                                       float zoomScale,
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
     * @brief Evaluates dynamic ink flow modulation from motion velocity, corner turn dynamics, and dwell time.
     *
     * Anti-blobbing / ink flow simulation pipeline:
     * 1. Corner turn compensation: Prevents momentary direction reversal slowdowns from bloating vertices.
     * 2. Base hydrodynamic speed thinning: Starvation thinning at high pen velocities.
     * 3. Time dwell accumulation: If timeDwellMultiplier > 0, dwell time increases flow (Mode B: spray/bleed).
     *
     * @param filteredSpeedMmPerSec Inertially filtered pen speed (mm/s).
     * @param turnAngleRad Angular turn delta between consecutive segments in radians [0, pi].
     * @param zoomScale Canvas viewport zoom scale.
     * @param config Active inking configuration.
     * @param dt Elapsed time interval (seconds) since last sample.
     * @return Dynamic ink flow factor.
     */
    [[nodiscard]] static float SimulateInkFlow(float filteredSpeedMmPerSec,
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

            constexpr float kPi = std::numbers::pi_v<float>;
            float denom = std::max(1e-4f, kPi - config.dynamics.cornerCompensationThresholdRad);
            float t = std::clamp((turnAngleRad - config.dynamics.cornerCompensationThresholdRad) / denom, 0.0f, 1.0f);
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
     * @brief Computes directional thinning factor for chisel/calligraphy blades.
     * @param travelHeadingRad Instantaneous direction of stroke motion in radians.
     * @param nibAngleRad Fixed orientation angle of the chisel blade in radians.
     * @param aspect Ratio of minor-to-major axis (e.g. 0.1 for sharp calligraphy, 1.0 for round).
     * @return Directional width multiplier in [aspect, 1.0].
     */
    [[nodiscard]] static float DirectionThinner(float travelHeadingRad,
                                                float nibAngleRad,
                                                float aspect) noexcept {
        if (aspect >= 0.999f) return 1.0f;
        const float a = aspect < 0.02f ? 0.02f : aspect;
        const float th = travelHeadingRad - nibAngleRad;
        const float s = std::sin(th);
        const float c = std::cos(th);
        return std::sqrt(s * s + a * a * c * c);
    }

    /**
     * @brief Clamps spatial slope (dv/ds) to prevent a stroke attribute from changing too abruptly over arc length.
     * @param targetValue Unconstrained target value (e.g., radius in mm, opacity).
     * @param previousValue Value at the previous modeled point.
     * @param segmentLengthMm Arc length between previous and current point (mm).
     * @param maxSlope Maximum allowable dv/ds slope change.
     * @return Spatially bounded value.
     */
    [[nodiscard]] static float ClampSpatialSlope(float targetValue,
                                                 float previousValue,
                                                 float segmentLengthMm,
                                                 float maxSlope) noexcept {

        // safety checks: if any parameter is invalid
        if (previousValue < 0.0f || segmentLengthMm <= 0.0f || maxSlope <= 0.0f) {
            return targetValue;
        }

        // compute the maximum allowed delta based on segment length and slope then clamp it
        float maxDelta = segmentLengthMm * maxSlope;
        return std::clamp(targetValue, previousValue - maxDelta, previousValue + maxDelta);
    }
};

} // namespace Folio
