/**
 * =========================================================================================
 * @file src/engine/ink_engine.cpp
 * @brief Core Vector Inking Engine & Stroke Modeler Orchestrator
 * =========================================================================================
 */

#include "engine/ink_engine.hpp"
#include "engine/stylus_dynamics.hpp"
#include "engine/post_processing/stroke_post_processor.hpp"
#include "engine/physics/ink_physics_bridge.hpp"
#include "engine/physics/ink_stroke_finisher.hpp"

#include <ink_stroke_modeler/stroke_modeler.h>
#include <algorithm>
#include <cmath>

namespace Folio {

namespace sm = ink::stroke_model;

// -----------------------------------------------------------------------------
// CONSTRUCTION & INITIALIZATION
// -----------------------------------------------------------------------------

InkEngine::InkEngine() {
    m_physicsBridge = std::make_unique<InkPhysicsBridge>();

    // Pre-allocate working vectors to prevent dynamic allocations during drawing
    m_activeStrokePoints.reserve(4096);
    m_liveModeledPoints.reserve(8192);
    m_physicsResultsBuffer.reserve(64);
    m_predictionResultsBuffer.reserve(64);
    m_predictedPointsBuffer.reserve(64);

    UpdatePhysicsParams();
    UpdateEvalConfig();
}

InkEngine::~InkEngine() = default;

// -----------------------------------------------------------------------------
// 1. INKING LIFECYCLE
// -----------------------------------------------------------------------------

void InkEngine::BeginStroke(double worldX, double worldY, float pressure, double timeSec,
                            const StrokeStyle& style, float zoomScale, float tiltX, float tiltY) {
    CancelStroke();
 
    // null-pointer safety guard
    if (!m_physicsBridge) {
        m_physicsBridge = std::make_unique<InkPhysicsBridge>();
        UpdatePhysicsParams();
    }

    m_isStrokeActive = true;
    m_activeStyle = style;
    UpdateEvalConfig();
    m_zoomScale = zoomScale > 0.001f ? zoomScale : 1.0f;

    /// -- Seeds the raw point buffer with the initial touch down and resets all kinematic variables

    // 1. Record initial stylus touchdown sample
    StylusInputPoint startPt(worldX, worldY, pressure, timeSec, 0.0f, tiltX, tiltY);
    m_activeStrokePoints.push_back(startPt);

    // 2. Initialize temporal dynamics tracking for acceleration and corner compensation
    m_filteredSpeedMmPerSec = 0.0f;
    m_lastPointTimeSec = timeSec;
    m_lastModeledTimeSec = timeSec;
    m_lastHeadingRad = 0.0f;
    m_hasHeading = false;
    m_currentTurnAngleRad = 0.0f;

    // 3. Compute initial line width (initial safety check for negative pressure values)
    float startPressure = (startPt.pressure >= 0.0f) ? startPt.pressure : 0.65f;
    float dynamicWidth = EvaluateTapWidth(startPressure);

    // 4. Seed immediate touchdown point into modeled centerline (0ms visual feedback while filter buffers;
    //    capsule/dab geometry is built lazily by the active renderer or brush composer)
    StrokePoint dotPt(startPt.x, startPt.y, dynamicWidth * 0.5f, startPressure, startPt.tiltX, startPt.tiltY, timeSec);
    m_liveModeledPoints.push_back(dotPt);
    m_capsulesDirty = true;
    m_validLiveSegments = 0;

    // 5. Seed the touchdown event into physics solver
    m_physicsBridge->Reset();
    if (m_physicsBridge->ProcessTouchDown(startPt, m_physicsResultsBuffer)) {
        ProcessPhysicsResults(m_physicsResultsBuffer);
    }

    // check if prediction is enabled and mark prediction as dirty to trigger a recalculation
    if (m_config.prediction.enable) {
        m_predictionDirty = true;
    }
}

void InkEngine::AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                            float tiltX, float tiltY) {

    // null-pointer safety guard
    if (!m_isStrokeActive || !m_physicsBridge) return;

    // 1. Enforce monotonic timestamp so out-of-order OS/driver events never move time backward
    timeSec = std::max(timeSec, m_lastPointTimeSec);
    m_lastPointTimeSec = timeSec;

    // 2. Record the new stylus sample into the raw point buffer
    StylusInputPoint pt(worldX, worldY, pressure, timeSec, 0.0f, tiltX, tiltY);
    m_activeStrokePoints.push_back(pt);

    // 3. Feed physics modeler (kinematic speed, heading, and turn-angle state are updated on modeled results)
    if (m_physicsBridge->ProcessMotion(pt, m_physicsResultsBuffer)) {
        ProcessPhysicsResults(m_physicsResultsBuffer);
    }

    // 4. Mark prediction as dirty 
    if (m_config.prediction.enable) {
        m_predictionDirty = true;
    }
}

FinishedStrokeData InkEngine::FinishStroke() {
    
    // null-pointer safety guard
    if (!m_isStrokeActive) return {};

    // Ensure physics bridge is initialized (lazy allocation)
    if (!m_physicsBridge) {
        m_physicsBridge = std::make_unique<InkPhysicsBridge>();
        UpdatePhysicsParams();
    }

    // Stage 1: Flush lingering physics inertia on pen lift
    if (!m_activeStrokePoints.empty()) {
    
        m_predictedPointsBuffer.clear();
        if (m_physicsBridge->ProcessTouchUp(m_activeStrokePoints.back(), m_physicsResultsBuffer)) {

            ProcessPhysicsResults(m_physicsResultsBuffer);
        }
    }

    // Stage 2: Handle stationary tap edge-case (synthesizes single dot if no lateral movement occurred)
    const float tapPressure = (!m_activeStrokePoints.empty() && m_activeStrokePoints.front().pressure >= 0.0f)
                                  ? m_activeStrokePoints.front().pressure
                                  : 0.65f;
    const float tapWidth = EvaluateTapWidth(tapPressure);
    InkStrokeFinisher::ResolveSingleTapEdgeCase(m_activeStrokePoints, tapWidth, m_liveModeledPoints);

    // Capture raw modeled points before any destructive post-processing stages
    std::vector<StrokePoint> rawPointsBeforePost = m_liveModeledPoints;

    // Stage 3: Optional Tail Tapering (for high-latency stylus devices))
    InkStrokeFinisher::ApplyTailTaper(m_config, m_activeStyle, m_liveModeledPoints);

    // Stage 3.5: Spatial Gaussian Radius Smoothing (Rounds sharp spikes into smooth hills)
    if (m_config.dynamics.enableGaussianRadiusSmoothing && m_liveModeledPoints.size() > 2) {
        StrokePostProcessor::SmoothRadiiGaussian(
            m_liveModeledPoints,
            m_config.dynamics.radiusSmoothingKernelSigmaMm,
            m_config.dynamics.radiusSmoothingPasses);
    }

    // Stage 4: Post-processing simplification (decimates redundant collinear vertices)
    if (m_config.postProcessing.enableSimplification && m_liveModeledPoints.size() > 2) {
        m_liveModeledPoints = StrokePostProcessor::Simplify(
            m_liveModeledPoints,
            m_config.postProcessing.simplificationToleranceMm,
            m_config.postProcessing.simplificationMaxTurnDeg,
            m_config.postProcessing.simplificationRadiusTolerance);
    }

    // Stage 5: Package finished stroke data
    FinishedStrokeData data;
    data.rawModeledPoints = std::move(rawPointsBeforePost);
    data.modeledPoints = std::move(m_liveModeledPoints);
    data.liveSegments  = InkStrokeFinisher::BuildCollisionSegments(data.modeledPoints);

    // Stage 6: Reset active state for next stroke
    CancelStroke();
    return data;
}

void InkEngine::CancelStroke() {
    m_activeStrokePoints.clear();
    m_liveModeledPoints.clear();
    m_physicsResultsBuffer.clear();
    m_predictionResultsBuffer.clear();
    m_predictedPointsBuffer.clear();
    m_liveCapsules.clear();
    m_predictedCapsules.clear();
    m_validLiveSegments = 0;
    m_capsulesDirty = false;
    m_predictionDirty = false;
    m_filteredSpeedMmPerSec = 0.0f;
    m_lastPointTimeSec = 0.0;
    m_lastModeledTimeSec = 0.0;
    m_lastHeadingRad = 0.0f;
    m_hasHeading = false;
    m_currentTurnAngleRad = 0.0f;
    m_isStrokeActive = false;
}

// -----------------------------------------------------------------------------
// 2. STATE & GEOMETRY ACCESSORS
// -----------------------------------------------------------------------------

const std::vector<CapsuleInstance>& InkEngine::GetLiveCapsules() const noexcept {

    if (m_capsulesDirty) {
        SyncLiveCapsules();
    }

    return m_liveCapsules;
}

const std::vector<CapsuleInstance>& InkEngine::GetPredictedCapsules() const noexcept {

    if (m_predictedCapsulesDirty) {
        BuildPredictedCapsules();
    }

    return m_predictedCapsules;
}

const std::vector<StrokePoint>& InkEngine::GetPredictedPoints() const noexcept {

    if (m_predictionDirty) {
        ComputePredictedPoints();
    }

    return m_predictedPointsBuffer;
}

// Catches m_liveCapsules up with the newest not-yet-rendered modeled points when the GPU asks for them.
void InkEngine::SyncLiveCapsules() const {

    // Check if the live capsule cache is dirty
    if (!m_capsulesDirty) return;
    m_capsulesDirty = false;

    // If there are no modeled points, clear the capsule cache and reset the valid segment count
    if (m_liveModeledPoints.empty()) {

        m_liveCapsules.clear();
        m_validLiveSegments = 0;
        return;
    }

    // If there is only one modeled point, build a single capsule instance and reset the valid segment count
    if (m_liveModeledPoints.size() == 1) {

        CapsuleBuilder::BuildCapsuleInstances(
            m_liveModeledPoints, PackPremultipliedGpuColor(m_activeStyle.color), m_liveCapsules);
        m_validLiveSegments = 0;
        return;
    }

    // Incremental build from m_validLiveSegments for O(1) per-packet append efficiency
    CapsuleBuilder::BuildCapsuleInstances(
        m_liveModeledPoints, PackPremultipliedGpuColor(m_activeStyle.color), m_liveCapsules, m_validLiveSegments);
    m_validLiveSegments = m_liveCapsules.size();
}

// Extrapolates future centerline points lazily on demand (capsule-free for universal consumers).
void InkEngine::ComputePredictedPoints() const {
    if (!m_predictionDirty) return;
    m_predictionDirty = false;
    m_predictedPointsBuffer.clear();

    // Early exit if prediction is disabled, no modeled points exist, or physics bridge is uninitialized
    if (!m_config.prediction.enable || m_liveModeledPoints.empty() || !m_physicsBridge) {
        return;
    }

    // Dynamic suppression: If moving below minimum speed, or executing a sharp turn, suppress lookahead prediction
    if (m_config.prediction.minSpeedMmPerSec > 0.0f && m_filteredSpeedMmPerSec < m_config.prediction.minSpeedMmPerSec) {
        return;
    }

    // Dynamic suppression: If the current turn angle exceeds the configured threshold, suppress lookahead prediction
    if (m_config.prediction.suppressOnTurns) {

        // Suppress prediction if the current turn angle exceeds the configured threshold
        if (m_currentTurnAngleRad > m_config.prediction.turnSuppressionThresholdRad) {
            return;
        }

        // Suppress prediction if the last three modeled points form a sharp corner (cosine of angle below threshold)
        if (m_liveModeledPoints.size() >= 3) {

            const auto& p1 = m_liveModeledPoints[m_liveModeledPoints.size() - 3];
            const auto& p2 = m_liveModeledPoints[m_liveModeledPoints.size() - 2];
            const auto& p3 = m_liveModeledPoints.back();
            float v1x = p2.x - p1.x, v1y = p2.y - p1.y;
            float v2x = p3.x - p2.x, v2y = p3.y - p2.y;
            float speed1 = std::hypot(v1x, v1y);
            float speed2 = std::hypot(v2x, v2y);

            // Only compute the cosine of the angle if both segments have non-zero length to avoid division by zero
            if (speed1 > 1e-4f && speed2 > 1e-4f) {

                float cosTheta = (v1x * v2x + v1y * v2y) / (speed1 * speed2);

                // If the cosine of the angle is below the configured threshold, suppress prediction to avoid whiskers
                if (cosTheta < m_config.prediction.maxCurvatureCosTheta) {

                    return; // Sharp direction change: cut prediction lookahead to eliminate whiskers
                }
            }
        }
    }

    // If we reach this point, prediction is enabled, the physics bridge is initialized, and the current stroke state allows for lookahead prediction. 
    // We can now invoke the physics bridge to generate predicted points based on the current modeled trajectory.
    if (!m_physicsBridge->Predict(m_predictionResultsBuffer)) {
        return;
    }

    m_predictedPointsBuffer.reserve(m_predictionResultsBuffer.size() + 1);
    m_predictedPointsBuffer.push_back(m_liveModeledPoints.back());

    // Convert predicted physics results into StrokePoint instances and append them to the predicted points buffer
    for (const auto& res : m_predictionResultsBuffer) {
        m_predictedPointsBuffer.push_back(ToStrokePoint(res));
    }

    // Build GPU capsule instances from the predicted points for rendering
    const size_t totalPredPts = m_predictedPointsBuffer.size();
    if (totalPredPts >= 2) {

        // Apply tip tapering to soften prediction snapping/flicker at the tip
        if (m_config.prediction.taperTip && totalPredPts > 2) {
            const float baseR = m_predictedPointsBuffer.front().r;
            const float minRatio = std::clamp(m_config.prediction.tipTaperRatio, 0.0f, 1.0f);
            for (size_t i = 1; i < totalPredPts; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(totalPredPts - 1);
                float taperFactor = 1.0f - (1.0f - minRatio) * t;
                m_predictedPointsBuffer[i].r = std::min(m_predictedPointsBuffer[i].r, baseR * taperFactor);
            }
        }
    }
}

// Builds predictive GPU capsules and distance-attenuated alpha fade when the GPU asks for them.
void InkEngine::BuildPredictedCapsules() const {

    if (!m_predictedCapsulesDirty) return;
    m_predictedCapsulesDirty = false;
    m_predictedCapsules.clear();

    // Early exit if prediction is disabled
    if (m_predictionDirty) {
        ComputePredictedPoints();
    }

    if (m_predictedPointsBuffer.size() >= 2) {
        CapsuleBuilder::BuildCapsuleInstances(
            m_predictedPointsBuffer, PackPremultipliedGpuColor(m_activeStyle.color), m_predictedCapsules);

        // Distance-attenuated alpha fading: confidence degrades with lookahead distance
        if (m_config.prediction.fadeEnable && !m_predictedCapsules.empty()) {

            float totalDist = 0.0f;
            for (const auto& cap : m_predictedCapsules) {
                float dx = cap.pointB.x - cap.pointA.x;
                float dy = cap.pointB.y - cap.pointA.y;
                totalDist += std::hypot(dx, dy);
            }

            // Compute the fade start distance and apply linear interpolation of alpha values along the predicted capsules
            const float fadeStart = std::max(0.0f, m_config.prediction.fadeStartMm);
            if (totalDist > fadeStart) {
                
                float fadeSpan = totalDist - fadeStart;
                float accumDist = 0.0f;
                for (auto& cap : m_predictedCapsules) {
                    float dx = cap.pointB.x - cap.pointA.x;
                    float dy = cap.pointB.y - cap.pointA.y;
                    float segLen = std::hypot(dx, dy);
                    float midDist = accumDist + 0.5f * segLen;
                    accumDist += segLen;

                    // Apply linear fade to the capsule's color based on its midpoint distance along the predicted path
                    if (midDist > fadeStart) {
                        
                        float t = std::clamp((midDist - fadeStart) / fadeSpan, 0.0f, 1.0f);
                        float fade = std::lerp(1.0f, m_config.prediction.fadeEndAlpha, t);
                        cap.colorRgba    = PackPremultipliedGpuColor(m_activeStyle.color, fade);
                        cap.colorRgbaEnd = cap.colorRgba;
                    }
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 3. MATHEMATICAL THICKNESS & MODELING
// -----------------------------------------------------------------------------

// Returns line thickness (mm) for a pressure. Tilt is not used here; it is routed to the brush composer.
float InkEngine::EvaluateTapWidth(float pressure) const noexcept {

    // Safety check: If pressure dynamics are disabled or the pressure value is negative
    if (!m_evalConfig.dynamics.usePressure || pressure < 0.0f) {
        return m_activeStyle.baseWidth;
    }

    // Evaluate pressure dynamics to compute a normalized pressure fraction [0.0, 1.0] and scale the base width accordingly
    float p = StylusDynamics::EvaluatePressure(pressure, m_evalConfig);
    float minFrac = std::min(m_evalConfig.dynamics.minWidthMultiplier, m_evalConfig.dynamics.maxWidthMultiplier);
    float maxFrac = std::max(m_evalConfig.dynamics.minWidthMultiplier, m_evalConfig.dynamics.maxWidthMultiplier);
    
    return m_activeStyle.baseWidth * std::clamp(minFrac + p * (maxFrac - minFrac), minFrac, maxFrac);
}

// converts a physics-modeled result into a StrokePoint for rendering and capsule building. 
StrokePoint InkEngine::ToStrokePoint(const sm::Result& res) const {

    float tiltRad = (res.tilt >= 0.0f) ? res.tilt : 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    // Convert polar tilt to Cartesian components if tilt is valid and orientation is defined
    if (tiltRad > 0.0f && res.orientation >= 0.0f) {
        tiltX = tiltRad * std::cos(res.orientation);
        tiltY = tiltRad * std::sin(res.orientation);
    }
    float effectivePressure = (res.pressure >= 0.0f) ? res.pressure : 0.65f;
    float dynamicWidth = EvaluateTapWidth(effectivePressure);

    return StrokePoint(res.position.x, res.position.y, dynamicWidth * 0.5f, effectivePressure, tiltX, tiltY, res.time.Value());
}

// Basicaly all magic happens here
void InkEngine::ProcessPhysicsResults(const std::vector<sm::Result>& results) {
    // Early exit if there are no results to process
    if (results.empty()) return;

    // Process each physics result to update live modeled points, applying filtering and slope clamping
    for (const auto& res : results) {

        // 1. Calculate physical speed and heading directly on modeled trajectory
        float resSpeed = std::hypot(res.velocity.x, res.velocity.y);
        double resTimeSec = res.time.Value();
        float dt = static_cast<float>(resTimeSec - m_lastModeledTimeSec);

        // Safety check: If this is the first modeled point or time delta is negative, use a default small dt
        if (m_lastModeledTimeSec <= 0.0 || dt < 0.0f) {

            dt = 1.0f / 180.0f;
        }
        m_lastModeledTimeSec = resTimeSec;

        // 2. Filter speed with acceleration limits on the modeled path
        if (m_evalConfig.dynamics.useAccelerationFiltering) {

            m_filteredSpeedMmPerSec = StylusDynamics::FilterVelocity(
                resSpeed,
                m_filteredSpeedMmPerSec,
                dt,
                m_evalConfig.dynamics.maxAccelerationMmPerSec2,
                m_evalConfig.dynamics.maxDecelerationMmPerSec2);
        } else {
            m_filteredSpeedMmPerSec = resSpeed;
        }

        // 3. Compute angular heading change along modeled path
        if (!m_liveModeledPoints.empty()) {

            float dx = static_cast<float>(res.position.x - m_liveModeledPoints.back().x);
            float dy = static_cast<float>(res.position.y - m_liveModeledPoints.back().y);
            float ds = std::hypot(dx, dy);

            // Ignore micro-jitter below 0.03mm to prevent heading noise from affecting turn angle calculations
            if (ds > 0.03f) {

                // Compute heading in radians using atan2 to get the angle of the vector (dx, dy)
                float heading = std::atan2(dy, dx);
                if (m_hasHeading) {

                    float diff = std::abs(heading - m_lastHeadingRad);
                    constexpr float kPi = 3.14159265358979323846f;
                    constexpr float kTwoPi = 2.0f * kPi;
                    // Normalize the angular difference to the range [0, 2π] to handle wrap-around cases
                    while (diff > kPi) {

                        diff = std::abs(diff - kTwoPi);
                    }
                    m_currentTurnAngleRad = diff;
                } else {

                    m_currentTurnAngleRad = 0.0f;
                }
                m_lastHeadingRad = heading;
                m_hasHeading = true;
            }
        }

        StrokePoint pt = ToStrokePoint(res);

        // 4. Clamp spatial radius slope (dr/ds) against previous point to prevent bloat during slow movements
        if (m_evalConfig.dynamics.enableSpatialSlopeClamping && !m_liveModeledPoints.empty()) {

            float ds = static_cast<float>(std::hypot(pt.x - m_liveModeledPoints.back().x,
                                                     pt.y - m_liveModeledPoints.back().y));
            pt.r = StylusDynamics::ClampSpatialSlope(
                pt.r,
                m_liveModeledPoints.back().r,
                ds,
                m_evalConfig.dynamics.maxRadiusSlopePerMm);
        }

        // Filter out stationary duplicates (< 1e-4 mm)
        if (m_liveModeledPoints.empty() ||
            std::hypot(pt.x - m_liveModeledPoints.back().x, pt.y - m_liveModeledPoints.back().y) >= 1e-4) {

            m_liveModeledPoints.push_back(pt);
        } else {

            // Update radius/telemetry on stationary initial point
            m_liveModeledPoints.back() = pt;
        }
    }

    // Mark the live capsule cache as dirty to trigger a rebuild on next GPU request
    if (!m_liveModeledPoints.empty()) {
        m_capsulesDirty = true;
    }
}

// -----------------------------------------------------------------------------
// 4. CONFIGURATION MANAGEMENT
// -----------------------------------------------------------------------------

void InkEngine::SetConfig(const InkingConfig& config) {
    m_config = config;
    UpdateEvalConfig();
    UpdatePhysicsParams();
}

// Caches zoom-scaled configuration parameters for tight loop evaluation
void InkEngine::UpdateEvalConfig() noexcept {
    m_evalConfig = m_config;
    m_evalConfig.dynamics.usePressure = m_config.dynamics.usePressure && m_activeStyle.usePressure;
    m_evalConfig.dynamics.useVelocity = m_config.dynamics.useVelocity && m_activeStyle.useVelocity;
    m_evalConfig.dynamics.useTilt     = m_config.dynamics.useTilt || m_activeStyle.useTilt;
}

// Updates the physics bridge with the current configuration parameters
void InkEngine::UpdatePhysicsParams() {
    if (m_physicsBridge) {
        m_physicsBridge->UpdateConfig(m_config);
    }
}

} // namespace Folio
