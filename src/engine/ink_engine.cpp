/**
 * =========================================================================================
 * @file src/engine/ink_engine.cpp
 * @brief Core Vector Inking Engine & Stroke Modeler Orchestrator
 * =========================================================================================
 */

#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_sketch/engine/stroke_width_evaluator.hpp"
#include "folio_sketch/engine/stroke_post_processor.hpp"
#include "ink_physics_bridge.hpp"
#include "ink_stroke_finisher.hpp"

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

    // 2. Compute initial line width
    float startPressure = (startPt.pressure >= 0.0f) ? startPt.pressure : 0.65f;
    float dynamicWidth = EvaluateTapWidth(startPressure, startPt.tiltX, startPt.tiltY);
    m_smoothedWidth = dynamicWidth;

    // 3. Emit immediate touchdown dot (0ms visual feedback while filter buffers)
    StrokePoint dotPt(startPt.x, startPt.y, dynamicWidth * 0.5f, startPressure, startPt.tiltX, startPt.tiltY);
    CapsuleBuilder::BuildCapsuleInstances(
        std::span<const StrokePoint>(&dotPt, 1),
        PackPremultipliedGpuColor(m_activeStyle.color),
        m_liveCapsules);
    m_capsulesDirty = false;

    // 4. Seed the touchdown event into physics solver
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

    // Stage 3: Optional Tail Tapering
    InkStrokeFinisher::ApplyTailTaper(m_config, m_activeStyle, m_liveModeledPoints);

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
    data.modeledPoints = m_liveModeledPoints;
    data.liveSegments  = InkStrokeFinisher::BuildCollisionSegments(m_liveModeledPoints);

    // Build GPU capsules
    CapsuleBuilder::BuildCapsuleInstances(
        m_liveModeledPoints, PackPremultipliedGpuColor(m_activeStyle.color), data.capsules);

    // Build 2D vector outline for SVG / Clipboard / PDF export
    data.outlinePath = InkGenerator::GenerateStroke(
        m_liveModeledPoints, m_activeStyle.capType, m_activeStyle.pattern);

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
    m_capsulesDirty = false;
    m_predictionDirty = false;
    m_smoothedWidth = -1.0f;
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
    CapsuleBuilder::BuildCapsuleInstances(
        m_liveModeledPoints, PackPremultipliedGpuColor(m_activeStyle.color), m_liveCapsules);
}

void InkEngine::EnsurePrediction() const {
    if (!m_predictionDirty) return;
    m_predictionDirty = false;
    m_predictedCapsules.clear();
    m_predictedPointsBuffer.clear();

    if (!m_config.prediction.enable || m_liveModeledPoints.empty() || !m_physicsBridge) {
        return;
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

    if (m_predictedPointsBuffer.size() >= 2) {
        CapsuleBuilder::BuildCapsuleInstances(
            m_predictedPointsBuffer, PackPremultipliedGpuColor(m_activeStyle.color), m_predictedCapsules);
    }
}

// -----------------------------------------------------------------------------
// 3. MATHEMATICAL THICKNESS & MODELING
// -----------------------------------------------------------------------------

float InkEngine::EvaluateTapWidth(float pressure, float tiltX, float tiltY) const noexcept {
    const bool isPencil = (m_activeStyle.pattern == StrokePattern::TexturedPencil);
    float pForEval = m_evalConfig.dynamics.usePressure ? pressure : -1.0f;
    return StrokeWidthEvaluator::CalculateDynamicWidth(
        m_activeStyle.baseWidth, pForEval, 0.0f, tiltX, tiltY, -1.0f, m_zoomScale, m_evalConfig, isPencil);
}

StrokePoint InkEngine::ToStrokePoint(const sm::Result& res, float previousWidth) const {
    float speed = (res.velocity.x != 0.0f || res.velocity.y != 0.0f)
                      ? std::hypot(res.velocity.x, res.velocity.y)
                      : 0.0f;
    float tiltRad = (res.tilt >= 0.0f) ? res.tilt : 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    if (tiltRad > 0.0f && res.orientation >= 0.0f) {
        tiltX = tiltRad * std::cos(res.orientation);
        tiltY = tiltRad * std::sin(res.orientation);
    }
    float effectivePressure = (res.pressure >= 0.0f) ? res.pressure : 0.65f;

    const bool isPencil = (m_activeStyle.pattern == StrokePattern::TexturedPencil);
    float pForEval = m_evalConfig.dynamics.usePressure ? effectivePressure : -1.0f;
    float vForEval = m_evalConfig.dynamics.useVelocity ? speed : 0.0f;

    float dynamicWidth = StrokeWidthEvaluator::CalculateDynamicWidthWithTiltRad(
        m_activeStyle.baseWidth, pForEval, vForEval, tiltRad,
        previousWidth, m_zoomScale, m_evalConfig, isPencil);

    return StrokePoint(res.position.x, res.position.y, dynamicWidth * 0.5f, effectivePressure, tiltX, tiltY);
}

void InkEngine::ProcessPhysicsResults(const std::vector<sm::Result>& results) {
    if (results.empty()) return;

    for (const auto& res : results) {
        StrokePoint pt = ToStrokePoint(res, m_smoothedWidth);
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
