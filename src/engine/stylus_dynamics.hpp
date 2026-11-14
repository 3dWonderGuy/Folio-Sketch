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
        if (dt <= 1e-5f) {
            return previousFilteredSpeed;
        }

        float deltaV = rawSpeed - previousFilteredSpeed;
        deltaV = std::clamp(deltaV, -maxDecel * dt, maxAccel * dt);
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
        if (!config.dynamics.useTilt || (tiltX == 0.0f && tiltY == 0.0f)) {
            return signal;
        }

        float tiltRad = std::hypot(tiltX, tiltY);
        if (tiltRad <= config.dynamics.tiltDeadzoneRad) {
            return signal;
        }

        signal.azimuthRad = std::atan2(tiltY, tiltX);
        signal.dirX = tiltX / tiltRad;
        signal.dirY = tiltY / tiltRad;

        if (tiltRad >= config.dynamics.tiltMaxAngleRad) {
            signal.magnitude = 1.0f;
            return signal;
        }

        float range = config.dynamics.tiltMaxAngleRad - config.dynamics.tiltDeadzoneRad;
        if (range <= 1e-5f) {
            signal.magnitude = 1.0f;
            return signal;
        }

        float t = std::clamp((tiltRad - config.dynamics.tiltDeadzoneRad) / range, 0.0f, 1.0f);
        // Smoothstep onset for natural shading transition
        signal.magnitude = t * t * (3.0f - 2.0f * t);
        return signal;
    }

    /**
     * @brief Clamps spatial cone-slope (dr/ds) to prevent radius expansion from outpacing arc length.
     * @param targetRadius Unconstrained target radius (mm).
     * @param previousRadius Radius of previous modeled point (mm).
     * @param segmentLengthMm Arc length between previous and current point (mm).
     * @param maxSlope Maximum allowable dr/ds slope change.
     * @return Spatially bounded radius (mm).
     */
    [[nodiscard]] static float ClampSpatialSlope(float targetRadius,
                                                 float previousRadius,
                                                 float segmentLengthMm,
                                                 float maxSlope) noexcept {
        if (previousRadius <= 0.0f || segmentLengthMm <= 0.0f || maxSlope <= 0.0f) {
            return targetRadius;
        }

        float maxDeltaR = segmentLengthMm * maxSlope;
        return std::clamp(targetRadius, previousRadius - maxDeltaR, previousRadius + maxDeltaR);
    }
};

} // namespace Folio
