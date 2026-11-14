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

    if (!m_physicsBridge) {
        m_physicsBridge = std::make_unique<InkPhysicsBridge>();
        UpdatePhysicsParams();
    }

    m_isStrokeActive = true;
    m_activeStyle = style;
    UpdateEvalConfig();
    m_zoomScale = zoomScale > 0.001f ? zoomScale : 1.0f;

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

    // 3. Compute initial line width
    float startPressure = (startPt.pressure >= 0.0f) ? startPt.pressure : 0.65f;
    float dynamicWidth = EvaluateTapWidth(startPressure, startPt.tiltX, startPt.tiltY);
    m_smoothedWidth = dynamicWidth;

    // 4. Emit immediate touchdown dot (0ms visual feedback while filter buffers)
    StrokePoint dotPt(startPt.x, startPt.y, dynamicWidth * 0.5f, startPressure, startPt.tiltX, startPt.tiltY, timeSec);
    CapsuleBuilder::BuildCapsuleInstances(
        std::span<const StrokePoint>(&dotPt, 1),
        PackPremultipliedGpuColor(m_activeStyle.color),
        m_liveCapsules);
    m_capsulesDirty = false;
    m_validLiveSegments = 0;

    // 5. Seed the touchdown event into physics solver
    m_physicsBridge->Reset();
    if (m_physicsBridge->ProcessTouchDown(startPt, m_physicsResultsBuffer)) {
        ProcessPhysicsResults(m_physicsResultsBuffer);
    }

    if (m_config.prediction.enable) {
        m_predictionDirty = true;
    }
}

void InkEngine::AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                            float tiltX, float tiltY) {
    if (!m_isStrokeActive || !m_physicsBridge) return;

    // 1. Calculate temporal and spatial deltas relative to previous input point
    float rawSpeed = 0.0f;
    float dt = static_cast<float>(timeSec - m_lastPointTimeSec);
    float dist = 0.0f;

    if (!m_activeStrokePoints.empty()) {
        const auto& prevPt = m_activeStrokePoints.back();
        float dx = static_cast<float>(worldX - prevPt.x);
        float dy = static_cast<float>(worldY - prevPt.y);
        dist = std::hypot(dx, dy);

        if (dt > 1e-5f) {
            rawSpeed = dist / dt;
        }

        // Angular heading & corner turn delta computation
        if (dist > 0.05f) { // Only evaluate heading if movement exceeds jitter threshold (0.05mm)
            float currentHeading = std::atan2(dy, dx);
            if (m_hasHeading) {
                float angleDiff = std::abs(currentHeading - m_lastHeadingRad);
                constexpr float kPi = 3.14159265358979323846f;
                constexpr float kTwoPi = 2.0f * kPi;
                while (angleDiff > kPi) {
                    angleDiff = std::abs(angleDiff - kTwoPi);
                }
                m_currentTurnAngleRad = angleDiff;
            } else {
                m_currentTurnAngleRad = 0.0f;
            }
            m_lastHeadingRad = currentHeading;
            m_hasHeading = true;
        }
    }

    m_lastPointTimeSec = timeSec;

    // 2. Filter speed using physical acceleration/deceleration constraints
    if (m_evalConfig.dynamics.useAccelerationFiltering) {
        m_filteredSpeedMmPerSec = StylusDynamics::FilterVelocity(
            rawSpeed,
            m_filteredSpeedMmPerSec,
            dt,
            m_evalConfig.dynamics.maxAccelerationMmPerSec2,
            m_evalConfig.dynamics.maxDecelerationMmPerSec2);
    } else {
        m_filteredSpeedMmPerSec = rawSpeed;
    }

    StylusInputPoint pt(worldX, worldY, pressure, timeSec, 0.0f, tiltX, tiltY);
    m_activeStrokePoints.push_back(pt);

    if (m_physicsBridge->ProcessMotion(pt, m_physicsResultsBuffer)) {
        ProcessPhysicsResults(m_physicsResultsBuffer);
    }

    if (m_config.prediction.enable) {
        m_predictionDirty = true;
    }
}

FinishedStrokeData InkEngine::FinishStroke() {
    if (!m_isStrokeActive) return {};

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
    const float tapWidth = EvaluateTapWidth(
        !m_activeStrokePoints.empty() ? m_activeStrokePoints.front().pressure : 0.65f,
        !m_activeStrokePoints.empty() ? m_activeStrokePoints.front().tiltX : 0.0f,
        !m_activeStrokePoints.empty() ? m_activeStrokePoints.front().tiltY : 0.0f);
    InkStrokeFinisher::ResolveSingleTapEdgeCase(m_activeStrokePoints, tapWidth, m_liveModeledPoints);

    // Capture raw modeled points before any destructive post-processing stages
    std::vector<StrokePoint> rawPointsBeforePost = m_liveModeledPoints;

    // Stage 3: Optional Tail Tapering
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
    data.modeledPoints = m_liveModeledPoints;
    data.liveSegments  = InkStrokeFinisher::BuildCollisionSegments(m_liveModeledPoints);

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
    m_smoothedWidth = -1.0f;
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
        EnsureLiveCapsules();
    }
    return m_liveCapsules;
}

const std::vector<CapsuleInstance>& InkEngine::GetPredictedCapsules() const noexcept {
    if (m_predictionDirty) {
        EnsurePrediction();
    }
    return m_predictedCapsules;
}

const std::vector<StrokePoint>& InkEngine::GetPredictedPoints() const noexcept {
    if (m_predictionDirty) {
        EnsurePrediction();
    }
    return m_predictedPointsBuffer;
}

void InkEngine::EnsureLiveCapsules() const {
    if (!m_capsulesDirty) return;
    m_capsulesDirty = false;
    if (m_liveModeledPoints.empty()) {
        m_liveCapsules.clear();
        m_validLiveSegments = 0;
        return;
    }
    if (m_liveModeledPoints.size() == 1) {
        CapsuleBuilder::BuildCapsuleInstances(
            m_liveModeledPoints, PackPremultipliedGpuColor(m_activeStyle.color), m_liveCapsules);
        m_validLiveSegments = 0;
        return;
    }
    // Incremental build from m_validLiveSegments for O(1) per-packet append efficiency
    CapsuleBuilder::AppendCapsuleInstances(
        m_liveModeledPoints, m_validLiveSegments, PackPremultipliedGpuColor(m_activeStyle.color), m_liveCapsules);
    m_validLiveSegments = m_liveCapsules.size();
}

void InkEngine::EnsurePrediction() const {
    if (!m_predictionDirty) return;
    m_predictionDirty = false;
    m_predictedCapsules.clear();
    m_predictedPointsBuffer.clear();

    if (!m_config.prediction.enable || m_liveModeledPoints.empty() || !m_physicsBridge) {
        return;
    }

    // Dynamic suppression: If moving below minimum speed, or executing a sharp turn, suppress lookahead prediction
    if (m_config.prediction.minSpeedMmPerSec > 0.0f && m_filteredSpeedMmPerSec < m_config.prediction.minSpeedMmPerSec) {
        return;
    }
    if (m_config.prediction.suppressOnTurns) {
        if (m_currentTurnAngleRad > m_config.prediction.turnSuppressionThresholdRad) {
            return;
        }
        if (m_liveModeledPoints.size() >= 3) {
            const auto& p1 = m_liveModeledPoints[m_liveModeledPoints.size() - 3];
            const auto& p2 = m_liveModeledPoints[m_liveModeledPoints.size() - 2];
            const auto& p3 = m_liveModeledPoints.back();
            float v1x = p2.x - p1.x, v1y = p2.y - p1.y;
            float v2x = p3.x - p2.x, v2y = p3.y - p2.y;
            float speed1 = std::hypot(v1x, v1y);
            float speed2 = std::hypot(v2x, v2y);
            if (speed1 > 1e-4f && speed2 > 1e-4f) {
                float cosTheta = (v1x * v2x + v1y * v2y) / (speed1 * speed2);
                if (cosTheta < m_config.prediction.maxCurvatureCosTheta) {
                    return; // Sharp direction change: cut prediction lookahead to eliminate whiskers
                }
            }
        }
    }

    if (!m_physicsBridge->Predict(m_predictionResultsBuffer)) {
        return;
    }

    float predWidth = m_smoothedWidth;
    m_predictedPointsBuffer.reserve(m_predictionResultsBuffer.size() + 1);
    m_predictedPointsBuffer.push_back(m_liveModeledPoints.back());

    for (const auto& res : m_predictionResultsBuffer) {
        StrokePoint pt = ToStrokePoint(res, predWidth);
        predWidth = pt.r * 2.0f;
        m_predictedPointsBuffer.push_back(pt);
    }

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

float InkEngine::EvaluateTapWidth(float pressure, float /*tiltX*/, float /*tiltY*/) const noexcept {
    if (!m_evalConfig.dynamics.usePressure || pressure < 0.0f) {
        return m_activeStyle.baseWidth;
    }
    float p = StylusDynamics::EvaluatePressure(pressure, m_evalConfig);
    float minFrac = std::min(m_evalConfig.dynamics.minWidthMultiplier, m_evalConfig.dynamics.maxWidthMultiplier);
    float maxFrac = std::max(m_evalConfig.dynamics.minWidthMultiplier, m_evalConfig.dynamics.maxWidthMultiplier);
    return m_activeStyle.baseWidth * std::clamp(minFrac + p * (maxFrac - minFrac), minFrac, maxFrac);
}

StrokePoint InkEngine::ToStrokePoint(const sm::Result& res, float previousWidth, float dt) const {
    (void)previousWidth;
    (void)dt;
    float tiltRad = (res.tilt >= 0.0f) ? res.tilt : 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    if (tiltRad > 0.0f && res.orientation >= 0.0f) {
        tiltX = tiltRad * std::cos(res.orientation);
        tiltY = tiltRad * std::sin(res.orientation);
    }
    float effectivePressure = (res.pressure >= 0.0f) ? res.pressure : 0.65f;
    float dynamicWidth = EvaluateTapWidth(effectivePressure, tiltX, tiltY);

    return StrokePoint(res.position.x, res.position.y, dynamicWidth * 0.5f, effectivePressure, tiltX, tiltY, res.time.Value());
}

void InkEngine::ProcessPhysicsResults(const std::vector<sm::Result>& results) {
    if (results.empty()) return;

    for (const auto& res : results) {
        // 1. Calculate physical speed and heading directly on modeled trajectory
        float resSpeed = (res.velocity.x != 0.0f || res.velocity.y != 0.0f)
                             ? std::hypot(res.velocity.x, res.velocity.y)
                             : 0.0f;
        double resTimeSec = res.time.Value();
        float dt = static_cast<float>(resTimeSec - m_lastModeledTimeSec);
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
            if (ds > 0.03f) { // Ignore sub-0.03mm micro-jitter
                float heading = std::atan2(dy, dx);
                if (m_hasHeading) {
                    float diff = std::abs(heading - m_lastHeadingRad);
                    constexpr float kPi = 3.14159265358979323846f;
                    constexpr float kTwoPi = 2.0f * kPi;
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

        StrokePoint pt = ToStrokePoint(res, m_smoothedWidth, dt);

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

        m_smoothedWidth = pt.r * 2.0f;

        // Filter out stationary duplicates (< 1e-4 mm)
        if (m_liveModeledPoints.empty() ||
            std::hypot(pt.x - m_liveModeledPoints.back().x, pt.y - m_liveModeledPoints.back().y) >= 1e-4) {
            m_liveModeledPoints.push_back(pt);
        } else {
            // Update radius/telemetry on stationary initial point
            m_liveModeledPoints.back() = pt;
        }
    }

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

void InkEngine::UpdateEvalConfig() noexcept {
    m_evalConfig = m_config;
    m_evalConfig.dynamics.usePressure = m_config.dynamics.usePressure && m_activeStyle.usePressure;
    m_evalConfig.dynamics.useVelocity = m_config.dynamics.useVelocity && m_activeStyle.useVelocity;
    m_evalConfig.dynamics.useTilt     = m_config.dynamics.useTilt || m_activeStyle.useTilt;
}

void InkEngine::UpdatePhysicsParams() {
    if (m_physicsBridge) {
        m_physicsBridge->UpdateConfig(m_config);
    }
}

} // namespace Folio
