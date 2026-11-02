#pragma once
/**
 * =========================================================================================
 * @file include/folio_sketch/ink_config.hpp
 * @brief Self-Sustained Inking Physics, Smoothing, and Calibration Configuration
 * =========================================================================================
 *
 * Defines runtime tuning parameters structured into modular domains:
 * 1. Calibration    - Hardware digitizer pressure curve & deadzone thresholds
 * 2. Physics        - Google Ink Stroke Modeler spring-mass-damper ODE parameters
 * 3. Filters        - Tremor/wobble filter and loop contraction mitigation
 * 4. Prediction     - Forward Kalman latency prediction lookahead
 * 5. Dynamics       - Pressure, velocity, and tilt-dependent line width evaluation
 * 6. PostProcessing - Submerged disk conditioning and stroke simplification
 */

#include <cmath>
#include <algorithm>

namespace Folio {

struct InkingConfig {
    // -------------------------------------------------------------------------
    // 1. HARDWARE DIGITIZER CALIBRATION
    // -------------------------------------------------------------------------
    struct Calibration {
        bool  enablePressureCurve          = true;
        float pressureGamma                = 1.0f;
        float pressureDeadzoneMin          = 0.02f;
        float pressureSaturationMax        = 0.95f;
        float minRelativeThicknessFraction = 0.15f;
    } calibration;

    // -------------------------------------------------------------------------
    // 2. STROKE MODELER SPRING PHYSICS
    // -------------------------------------------------------------------------
    struct Physics {
        float springMassConstant           = 11.0f / 32400.0f; // ~0.000339
        float dragConstant                 = 72.0f;
        float minOutputRateHz              = 180.0f;
        float endOfStrokeStoppingDistanceMm = 0.01f;
        int   endOfStrokeMaxIterations     = 20;
        bool  enableAngleUpsampling        = true;
        float maxEstimatedAngleRad         = 0.06f;            // ~3.4 deg turn threshold
        bool  useStrokeNormalProjection    = false;
    } physics;

    // -------------------------------------------------------------------------
    // 3. MOTION FILTERS (Tremor & Loop Mitigation)
    // -------------------------------------------------------------------------
    struct Filters {
        // Hand Tremor / Noise Smoother
        bool  enableWobble                 = true;
        float wobbleTimeoutSec             = 0.04f;
        float wobbleSpeedFloor             = 1.31f;
        float wobbleSpeedCeiling           = 1.44f;

        // Speed-Based Loop Contraction Mitigation (Disabled by default)
        bool  enableLoopMitigation         = false;
        float loopSpeedLowerBound          = 100.0f;
        float loopSpeedUpperBound          = 1500.0f;
        float loopStrengthAtLowerSpeed     = 1.0f;
        float loopStrengthAtUpperSpeed     = 0.0f;
        float loopMinSpeedWindowSec        = 0.04f;
    } filters;

    // -------------------------------------------------------------------------
    // 4. FORWARD LATENCY PREDICTION
    // -------------------------------------------------------------------------
    struct Prediction {
        bool  enable                       = true;
        float durationMs                   = 16.0f;
    } prediction;

    // -------------------------------------------------------------------------
    // 5. DYNAMIC LINE WIDTH & TILT DYNAMICS
    // -------------------------------------------------------------------------
    struct Dynamics {
        bool  usePressure                  = true;
        bool  useVelocity                  = false;
        bool  useTilt                      = false;
        float minWidthMultiplier           = 0.2f;
        float maxWidthMultiplier           = 1.5f;
        float maxTiltWidthMultiplier       = 2.8f;
        float tiltDeadzoneRad              = 0.22f;            // ~12.6 deg upright threshold
        float tiltMaxAngleRad              = 1.05f;            // ~60 deg full shading angle
        float velocityThinningMaxSpeed     = 1500.0f;          // mm/s
        float widthSmoothingEma            = 0.6f;             // EMA factor [0.0, 1.0]
        float tailTaperLengthMm            = 0.0f;             // 0.0 = disabled
        float tailTaperPower               = 1.0f;
    } dynamics;

    // -------------------------------------------------------------------------
    // 6. POINT CONDITIONING & SIMPLIFICATION
    // -------------------------------------------------------------------------
    struct PostProcessing {
        bool  stationaryEnvelopment        = true;             // Merges points where ds <= |dr|
        bool  enableSimplification         = false;
        float simplificationToleranceMm    = 0.015f;           // RDP spatial tolerance
        float simplificationMaxTurnDeg     = 4.0f;             // Max angular chord deviation
        float simplificationRadiusTolerance = 0.08f;           // Max relative radius error
        float minTurnAngleForJointCapDeg   = 3.0f;
    } postProcessing;
};

using InkingTuningConfig = InkingConfig;

} // namespace Folio
