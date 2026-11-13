/**
 * =========================================================================================
 * @file src/engine/ink_physics_bridge.cpp
 * @brief Physics Engine Bridge: Google Ink Stroke Modeler Integration
 * =========================================================================================
 */

#include "ink_physics_bridge.hpp"

#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/params.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

namespace sm = ink::stroke_model;

namespace {

/**
 * @brief Translates our StylusInputPoint into Google Ink's Input packet.
 * Converts physical mm coordinates, time, pressure, and calculates polar tilt/orientation.
 */
sm::Input MakeGoogleInput(sm::Input::EventType type, const StylusInputPoint& pt) {
    sm::Input input;
    input.event_type = type;
    input.position = { static_cast<float>(pt.x), static_cast<float>(pt.y) };
    input.time = sm::Time(pt.TimeSec());
    input.pressure = pt.pressure;

    // Fast path for non-tilt devices (mouse, standard pens) to avoid trigonometric calls
    if (pt.tiltX == 0.0f && pt.tiltY == 0.0f) {
        input.tilt = 0.0f;
        input.orientation = 0.0f;
    } else {
        // Polar tilt magnitude clamped to [0, pi/2]
        input.tilt = std::clamp(std::hypot(pt.tiltX, pt.tiltY), 0.0f, 1.570796f);
        // Polar azimuth angle [-pi, pi] mapped to [0, 2*pi)
        float angle = std::atan2(pt.tiltY, pt.tiltX);
        input.orientation = angle < 0.0f ? angle + 2.0f * static_cast<float>(M_PI) : angle;
    }
    return input;
}

} // namespace

InkPhysicsBridge::InkPhysicsBridge() {
    m_modeler = std::make_unique<sm::StrokeModeler>();
}

InkPhysicsBridge::~InkPhysicsBridge() = default;

void InkPhysicsBridge::UpdateConfig(const InkingConfig& config) {
    if (!m_modeler) return;

    sm::StrokeModelParams params;

    // 1. Position Modeler (Spring-Mass-Damper ODE)
    params.position_modeler_params.spring_mass_constant = std::max(1e-6f, config.physics.springMassConstant);
    params.position_modeler_params.drag_constant        = std::max(1.0f, config.physics.dragConstant);

    // 2. Loop Contraction Mitigation
    auto& loopParams = params.position_modeler_params.loop_contraction_mitigation_params;
    loopParams.is_enabled = config.filters.enableLoopMitigation;
    if (loopParams.is_enabled) {
        loopParams.speed_lower_bound = std::max(0.0f, config.filters.loopSpeedLowerBound);
        loopParams.speed_upper_bound = std::max(loopParams.speed_lower_bound, config.filters.loopSpeedUpperBound);
        loopParams.interpolation_strength_at_speed_lower_bound =
            std::clamp(config.filters.loopStrengthAtLowerSpeed, 0.0f, 1.0f);
        loopParams.interpolation_strength_at_speed_upper_bound =
            std::clamp(config.filters.loopStrengthAtUpperSpeed, 0.0f,
                       loopParams.interpolation_strength_at_speed_lower_bound);
        loopParams.min_speed_sampling_window = sm::Duration(std::max(0.0f, config.filters.loopMinSpeedWindowSec));
    }

    // 3. Stylus State Modeler
    params.stylus_state_modeler_params.use_stroke_normal_projection =
        config.physics.useStrokeNormalProjection || config.filters.enableLoopMitigation;

    // 4. Wobble Smoother (Tremor / Noise Filter)
    params.wobble_smoother_params.is_enabled    = config.filters.enableWobble;
    params.wobble_smoother_params.timeout       = sm::Duration(std::max(0.0f, config.filters.wobbleTimeoutSec));
    params.wobble_smoother_params.speed_floor   = std::max(0.0f, config.filters.wobbleSpeedFloor);
    params.wobble_smoother_params.speed_ceiling = std::max(params.wobble_smoother_params.speed_floor,
                                                           config.filters.wobbleSpeedCeiling);

    // 5. Sampling Parameters & Angular Upsampling
    params.sampling_params.min_output_rate = std::max(10.0, static_cast<double>(config.physics.minOutputRateHz));
    params.sampling_params.end_of_stroke_stopping_distance =
        std::max(0.0001f, config.physics.endOfStrokeStoppingDistanceMm);
    params.sampling_params.end_of_stroke_max_iterations =
        std::clamp(config.physics.endOfStrokeMaxIterations, 1, 1000);
    params.sampling_params.max_estimated_angle_to_traverse_per_input =
        config.physics.enableAngleUpsampling
            ? std::clamp(static_cast<double>(config.physics.maxEstimatedAngleRad), 0.005, 3.10)
            : -1.0;

    // 6. Forward Predictor Configuration (Kalman lookahead vs StrokeEnd vs Disabled)
    if (!config.prediction.enable) {
        params.prediction_params = sm::DisabledPredictorParams{};
    } else if (config.prediction.durationMs > 0.0f) {
        sm::KalmanPredictorParams kalman;
        kalman.process_noise = static_cast<double>(config.prediction.processNoise);
        kalman.measurement_noise = static_cast<double>(config.prediction.measurementNoise);
        kalman.min_stable_iteration = std::max(1, config.prediction.minStableIteration);
        kalman.max_time_samples = 20;
        kalman.min_catchup_velocity = config.prediction.minCatchupVelocity;
        kalman.acceleration_weight = config.prediction.accelerationWeight;
        kalman.jerk_weight = config.prediction.jerkWeight;
        kalman.prediction_interval = sm::Duration(std::max(0.001f, config.prediction.durationMs * 0.001f));
        kalman.confidence_params.desired_number_of_samples = 20;
        kalman.confidence_params.max_estimation_distance = 1.5f;
        kalman.confidence_params.min_travel_speed = std::max(1.0f, config.prediction.minSpeedMmPerSec * 0.1f);
        kalman.confidence_params.max_travel_speed = 25.0f;
        kalman.confidence_params.max_linear_deviation = 5.0f;
        kalman.confidence_params.baseline_linearity_confidence = 0.5f;
        params.prediction_params = kalman;
    } else {
        params.prediction_params = sm::StrokeEndPredictorParams{};
    }

    // Initialize/re-initialize physics parameters
    if (!m_modeler->Reset(params).ok()) {
        params.prediction_params = sm::StrokeEndPredictorParams{};
        (void)m_modeler->Reset(params);
    }
}

void InkPhysicsBridge::Reset() {
    if (m_modeler) {
        (void)m_modeler->Reset();
    }
}

bool InkPhysicsBridge::ProcessTouchDown(const StylusInputPoint& pt, std::vector<ink::stroke_model::Result>& outResults) {
    if (!m_modeler) return false;
    outResults.clear();
    return m_modeler->Update(MakeGoogleInput(sm::Input::EventType::kDown, pt), outResults).ok();
}

bool InkPhysicsBridge::ProcessMotion(const StylusInputPoint& pt, std::vector<ink::stroke_model::Result>& outResults) {
    if (!m_modeler) return false;
    outResults.clear();
    return m_modeler->Update(MakeGoogleInput(sm::Input::EventType::kMove, pt), outResults).ok();
}

bool InkPhysicsBridge::ProcessTouchUp(const StylusInputPoint& pt, std::vector<ink::stroke_model::Result>& outResults) {
    if (!m_modeler) return false;
    outResults.clear();
    return m_modeler->Update(MakeGoogleInput(sm::Input::EventType::kUp, pt), outResults).ok();
}

bool InkPhysicsBridge::Predict(std::vector<ink::stroke_model::Result>& outPredictions) {
    if (!m_modeler) return false;
    outPredictions.clear();
    return m_modeler->Predict(outPredictions).ok() && !outPredictions.empty();
}

} // namespace Folio
