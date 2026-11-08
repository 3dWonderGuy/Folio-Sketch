#pragma once
/**
 * =========================================================================================
 * @file include/folio_sketch/core/ink_config.hpp
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
        bool  enablePressureCurve          = true;             // Enables deadzone, saturation, and gamma mapping on raw stylus pressure
        float pressureGamma                = 1.0f;             // Power-law exponent: <1.0 soft touch (thicker earlier), >1.0 firm touch
        float pressureDeadzoneMin          = 0.02f;            // Hardware pressure threshold [0,1] below which touch is treated as zero (ignores hovering noise)
        float pressureSaturationMax        = 0.95f;            // Hardware pressure threshold [0,1] at which 100% maximum stroke pressure is reached
    } calibration;

    // -------------------------------------------------------------------------
    // 2. STROKE MODELER SPRING PHYSICS
    // -------------------------------------------------------------------------
    struct Physics {
        float springMassConstant           = 11.0f / 32400.0f; // Spring stiffness k/m ratio (~0.000339): higher = tighter tracking, lower = smoother trailing
        float dragConstant                 = 72.0f;            // Damping coefficient gamma: prevents oscillation/overshoot around corners
        float minOutputRateHz              = 180.0f;           // Minimum physics update frequency; upsamples slow stylus movements to eliminate stepped corners
        float endOfStrokeStoppingDistanceMm = 0.01f;           // Euclidean distance threshold to consider the trailing pen tip fully settled at stroke lift
        int   endOfStrokeMaxIterations     = 20;               // Maximum relaxation iterations executed on pen-up to drain remaining spring inertia
        bool  enableAngleUpsampling        = true;             // Inserts intermediate vertices when sharp angular turns exceed maxEstimatedAngleRad
        float maxEstimatedAngleRad         = 0.06f;            // Angular turn threshold (~3.4 deg) triggering intermediate curve subdivision
        bool  useStrokeNormalProjection    = false;            // Projects spring-mass offset perpendicular to stroke tangent to mitigate corner cutting
    } physics;

    // -------------------------------------------------------------------------
    // 3. MOTION FILTERS (Tremor & Loop Mitigation)
    // -------------------------------------------------------------------------
    struct Filters {
        // Hand Tremor / Noise Smoother
        bool  enableWobble                 = true;             // Enables Google Ink wobble filter to cancel micro-tremor jitter at slow speeds
        float wobbleTimeoutSec             = 0.04f;            // Time window (seconds) to hold and smooth slow stationary pen jitters
        float wobbleSpeedFloor             = 1.31f;            // Speed threshold (mm/s) below which 100% maximum tremor smoothing is applied
        float wobbleSpeedCeiling           = 1.44f;            // Speed threshold (mm/s) above which wobble smoothing fades completely to 0%

        // Speed-Based Loop Contraction Mitigation (Disabled by default)
        bool  enableLoopMitigation         = false;            // Preserves intended tight cursive loops from being shrunk by spring damping
        float loopSpeedLowerBound          = 100.0f;           // Speed threshold (mm/s) where loop contraction mitigation begins taking effect
        float loopSpeedUpperBound          = 1500.0f;          // Speed threshold (mm/s) where full loop contraction mitigation strength is reached
        float loopStrengthAtLowerSpeed     = 1.0f;             // Interpolation weight at lower speed bound (1.0 = standard spring-mass behavior)
        float loopStrengthAtUpperSpeed     = 0.0f;             // Interpolation weight at upper speed bound (0.0 = pull position directly to raw input)
        float loopMinSpeedWindowSec        = 0.04f;            // Temporal window (seconds) used to sample minimum pen speed for loop classification
    } filters;

    // -------------------------------------------------------------------------
    // 4. FORWARD LATENCY PREDICTION
    // -------------------------------------------------------------------------
    struct Prediction {
        bool  enable                       = true;             // Predicts stroke trajectory ahead of physical digitizer events to reduce perceived latency
        float durationMs                   = 10.0f;            // Lookahead time (milliseconds) for the Kalman forward predictor (default ~10ms for stable tracking)
        float accelerationWeight           = 0.0f;             // Weight on acceleration (0.0 = constant velocity, suppresses wild overshoot on direction changes)
        float jerkWeight                   = 0.0f;             // Weight on jerk (higher values induce high-frequency oscillations / jitter)
        float processNoise                 = 0.067f;           // Process noise variance for Kalman filter
        float measurementNoise             = 0.25f;            // Measurement noise variance (higher = filters out noisy digitizer jitters)
        int   minStableIteration           = 4;                // Minimum iterations before predictor outputs extrapolation
        float minCatchupVelocity           = 0.01f;            // Minimum catchup velocity (mm/s)
        
        // Dynamic suppression & stabilization
        bool  suppressOnTurns              = true;             // Suppress/cut prediction lookahead during sharp angular direction changes
        float turnSuppressionThresholdRad  = 0.52359877f;      // Angular turning delta (~30 deg) above which prediction is suppressed
        float maxCurvatureCosTheta         = 0.866f;           // Dot product cos(theta) threshold (~30 deg): sharp turns below this cut prediction lookahead to eliminate whiskers
        float minSpeedMmPerSec             = 20.0f;            // Speed threshold (mm/s) below which prediction lookahead is faded out to prevent stationary jitter
        bool  taperTip                     = true;             // Gradually tapers radius towards the predicted tip to eliminate visual popping/snapping
        float tipTaperRatio                = 0.3f;             // Minimum radius multiplier at the tip of the prediction (relative to base radius)

        // Distance-attenuated alpha fade of the predicted tail (the further ahead, the less confident)
        bool  fadeEnable                   = true;             // Fades the predicted tail's alpha with distance from the confirmed tip
        float fadeStartMm                  = 0.4f;             // Predicted travel (mm) before the fade begins (avoids a jarring fade on short predictions)
        float fadeEndAlpha                 = 0.0f;             // Alpha multiplier reached at the far end of the prediction
    } prediction;

    // -------------------------------------------------------------------------
    // 5. DYNAMIC LINE WIDTH & TILT DYNAMICS
    // -------------------------------------------------------------------------
    struct Dynamics {
        bool  usePressure                  = true;             // Modulates line width dynamically according to calibrated stylus pressure
        bool  useVelocity                  = false;            // Modulates line width dynamically according to pen travel velocity (fountain pen thinning)
        bool  useTilt                      = false;            // Modulates line width and shading spread according to stylus inclination angle
        float minWidthMultiplier           = 0.2f;             // Minimum line width fraction relative to baseWidth at zero pressure or max velocity
        float maxWidthMultiplier           = 1.5f;             // Maximum line width fraction relative to baseWidth at 100% saturation pressure
        float maxTiltWidthMultiplier       = 2.8f;             // Maximum width expansion factor when stylus is tilted flat (e.g., pencil side shading)
        float tiltDeadzoneRad              = 0.22f;            // Angular tilt threshold (~12.6 deg from upright) below which tilt modulation is ignored
        float tiltMaxAngleRad              = 1.05f;            // Angular tilt threshold (~60 deg from upright) at which maximum shading width is reached
        float velocityThinningMaxSpeed     = 1500.0f;          // Pen velocity (mm/s) at which maximum hydrodynamic velocity thinning occurs
        float widthSmoothingEma            = 0.6f;             // Exponential Moving Average filter [0.0, 1.0] to eliminate abrupt step jumps in stroke radius
        float velocitySmoothingEma         = 0.25f;            // Dedicated EMA smoothing factor [0.0, 1.0] for velocity thinning to eliminate digitizer speed noise
        bool  useAccelerationFiltering     = true;             // Enables inertial speed filtering bounded by physical acceleration and deceleration limits
        float maxAccelerationMmPerSec2     = 3500.0f;          // Maximum physical acceleration ceiling (mm/s^2) for velocity thinning transitions
        float maxDecelerationMmPerSec2     = 2500.0f;          // Asymmetric deceleration limit (mm/s^2) preventing sudden line blooming upon pen deceleration
        bool  useCornerCompensation        = true;             // Enables turn-rate speed compensation to suppress corner bloat at sharp vertices
        float cornerCompensationThresholdRad = 0.52359877f;    // Angular delta threshold (~30 deg) above which corner velocity attenuation begins
        bool  enableSpatialSlopeClamping   = true;             // Enables cone-slope clamp (dr/ds) to prevent radius blooming during low-displacement movements
        float maxRadiusSlopePerMm          = 0.35f;            // Spatial cone-slope clamp (dr/ds) ceiling in mm (e.g. 0.05 tight.. 10.0 unconstrained)
        bool  enableGaussianRadiusSmoothing = true;            // Post-evaluates stroke radius profile with Gaussian filter to round off sharp peaks
        float radiusSmoothingKernelSigmaMm = 3.5f;             // Spatial Gaussian kernel standard deviation (mm) along stroke arc-length
        int   radiusSmoothingPasses        = 2;                // Number of radius smoothing iterations (rounds sharp peaks into smooth hills)
        float tailTaperLengthMm            = 0.0f;             // Arc length (mm) at stroke tail over which radius tapers down to minWidth (0 = disabled)
        float tailTaperPower               = 1.0f;             // Taper falloff exponent (1.0 = linear taper, >1.0 = rapid tip falloff)
    } dynamics;

    // -------------------------------------------------------------------------
    // 6. POINT CONDITIONING & SIMPLIFICATION
    // -------------------------------------------------------------------------
    struct PostProcessing {
        bool  stationaryEnvelopment        = true;             // Prunes redundant interior points where center distance ds <= radius delta |dr|
        bool  enableSimplification         = false;            // Decimates redundant collinear vertices upon stroke completion (RDP algorithm)
        float simplificationToleranceMm    = 0.015f;           // Maximum perpendicular distance error (mm) permitted during RDP curve decimation
        float simplificationMaxTurnDeg     = 4.0f;             // Maximum angular chord deviation (degrees) permitted during curve simplification
        float simplificationRadiusTolerance = 0.08f;           // Maximum relative radius change permitted before retaining a simplified vertex
        float minTurnAngleForJointCapDeg   = 3.0f;             // Minimum corner turning angle required before generating round pivot joint caps
    } postProcessing;

    // -------------------------------------------------------------------------
    // 7. SHAPE SNAPPING & RECOGNITION
    // -------------------------------------------------------------------------
    struct ShapeSnapping {
        bool  enableHoldToSnap             = true;             // Enables gesture hold-dwell detection to trigger geometric shape snapping
        float holdDwellDurationSec         = 0.5f;             // Stationary hold dwell duration (seconds) required to trigger shape snapping
        float movementThresholdMm          = 1.5f;             // Maximum movement radius (mm) allowed during the dwell window to be considered stationary
        float loopClosureGapMm             = 5.0f;             // Maximum gap distance (mm) between stroke endpoints to qualify as a closed loop
        float lineStraightnessToleranceMm  = 1.0f;             // Maximum chord deviation (mm) to classify an open stroke as a straight line
        float circleAspectTolerance        = 0.18f;            // Maximum eccentricity / aspect ratio deviation (|w - h| / max(w, h)) for circle fitting
        float ellipseFitToleranceMm        = 1.5f;             // Maximum mean radial deviation (mm) for ellipse fitting
        float polygonCornerAngleThresholdDeg = 45.0f;         // Minimum angular change (degrees) at RDP vertices to qualify as a polygon corner
    } shapeSnapping;
};

using InkingTuningConfig = InkingConfig;

} // namespace Folio
