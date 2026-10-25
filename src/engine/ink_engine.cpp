#include "folio_sketch/ink_engine.hpp"
#include "folio_sketch/stroke_width_evaluator.hpp"
#include "folio_sketch/ink_generator.hpp"
#include "folio_sketch/stroke_post_processor.hpp"

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

// translates our StylusInputPoint into Google Ink's Input packet
sm::Input MakeGoogleInput(sm::Input::EventType type, const StylusInputPoint& pt) {
    sm::Input input;                                                                   // empty google ink input packet
    input.event_type = type;                                                           // pen state (kDown, kMove, or kUp)
    input.position = { static_cast<float>(pt.x), static_cast<float>(pt.y) };           // convert double world mm to google float Vec2
    input.time = sm::Time(pt.timeSeconds);                                             // wrap timestamp into google Time struct
    input.pressure = pt.pressure;                                                      // stylus pressure [0.0, 1.0]

    // Fast short-circuit for non-tilt devices (mouse, standard pens) to avoid transcendental math
    if (pt.tiltX == 0.0f && pt.tiltY == 0.0f) {
        input.tilt = 0.0f;
        input.orientation = 0.0f;
    } else {
        input.tilt = std::clamp(std::hypot(pt.tiltX, pt.tiltY), 0.0f, 1.570796f);          // polar tilt magnitude clamped to [0, pi/2]
        float angle = std::atan2(pt.tiltY, pt.tiltX);                                      // polar azimuth angle [-pi, pi]
        input.orientation = angle < 0.0f ? angle + 2.0f * static_cast<float>(M_PI) : angle;// wrap orientation into [0, 2*pi)
    }
    return input;
}

} // namespace

InkEngine::InkEngine() {
    m_googleModeler = std::make_unique<sm::StrokeModeler>();

    // pre-allocate buffers once to prevent mid-stroke std::vector reallocations
    m_activeStrokePoints.reserve(4096);
    m_liveModeledPoints.reserve(8192);
    m_googleResultsBuffer.reserve(64);
    m_googlePredictionsBuffer.reserve(64);
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
    // Reset transient stroke accumulation buffers and discard any unfinished gesture
    CancelStroke();

    // Lazy initialization safety check: ensure the physics engine exists
    if (!m_googleModeler) {
        m_googleModeler = std::make_unique<sm::StrokeModeler>();
        UpdatePhysicsParams(); // Push config parameters (spring-mass, drag, wobble) into Google Modeler
    }

    // Initialize active gesture state flags and style
    m_isStrokeActive = true;
    m_activeStyle = style;
    UpdateEvalConfig(); // Recompute cached dynamic width flags (pressure, tilt, velocity enablement)
    m_zoomScale = zoomScale > 0.001f ? zoomScale : 1.0f;

    // Record raw digitizer touchdown telemetry
    StylusInputPoint startPt(worldX, worldY, pressure, timeSec, 0.0f, tiltX, tiltY);
    m_activeStrokePoints.push_back(startPt);

    // Compute initial dynamic line thickness at touchdown
    float startPressure = (startPt.pressure >= 0.0f) ? startPt.pressure : 0.65f;
    float dynamicWidth = EvaluateTapWidth(startPressure, startPt.tiltX, startPt.tiltY);
    m_smoothedWidth = dynamicWidth;

    // Synthesize an immediate visual fallback dot at touchdown:
    // Google Modeler buffers initial samples inside its wobble/tremor filter before producing output.
    // Generating this dot ensures immediate 0ms visual feedback at the pen tip.
    StrokePoint dotPt(startPt.x, startPt.y, dynamicWidth * 0.5f, startPressure, startPt.tiltX, startPt.tiltY);

    InkMeshBuilder::BuildSolidMesh(
        std::span<const StrokePoint>(&dotPt, 1),
        m_activeStyle.capType,
        PackGpuColor(m_activeStyle.color),
        m_liveStrokeMesh,
        m_zoomScale);
    m_meshDirty = false;

    // Reset physics modeler state for this new stroke (retains active physics parameters)
    (void)m_googleModeler->Reset();

    // Seed the touchdown event (kDown) into Google Stroke Modeler
    m_googleResultsBuffer.clear();
    if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kDown, startPt), m_googleResultsBuffer).ok()) {
        ProcessGoogleResults(m_googleResultsBuffer);
    }

    // Flag predictive tip geometry dirty to refresh latency-hiding projections
    if (m_config.google_enable_prediction) {
        m_predictionDirty = true;
    }
}

void InkEngine::AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                            float tiltX, float tiltY) {
    // Discard input if gesture is inactive or physics engine uninitialized
    if (!m_isStrokeActive || !m_googleModeler) return;

    // Ingest raw hardware digitizer telemetry
    StylusInputPoint pt(worldX, worldY, pressure, timeSec, 0.0f, tiltX, tiltY);
    m_activeStrokePoints.push_back(pt);

    // Feed the stylus motion event (kMove) into Google Stroke Modeler
    m_googleResultsBuffer.clear();
    if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kMove, pt), m_googleResultsBuffer).ok()) {
        // Convert physics outputs (position, velocity, pressure) into renderable centerline vertices
        ProcessGoogleResults(m_googleResultsBuffer);
    }

    // Trigger forward-prediction re-computation for the next frame
    if (m_config.google_enable_prediction) {
        m_predictionDirty = true;
    }
}

void InkEngine::FlushPhysicsOnPenUp() {
    if (m_activeStrokePoints.empty() || !m_googleModeler) return;

    m_googleResultsBuffer.clear();
    if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kUp, m_activeStrokePoints.back()), m_googleResultsBuffer).ok()) {
        // Upon pen lift, do not append speculative forward-prediction points as permanent stroke geometry.
        // Speculative points can curl erratically when pen velocity drops sharply before lift.
        // Instead, simply process the physical flushed modeler results as the spring relaxes to the lift position.
        m_predictedPointsBuffer.clear();
        ProcessGoogleResults(m_googleResultsBuffer);
    }
}

void InkEngine::HandleSingleTapEdgeCase() {
    if (m_activeStrokePoints.empty()) return;

    double strokeLen = 0.0;
    for (size_t i = 1; i < m_liveModeledPoints.size(); ++i) {
        strokeLen += std::hypot(m_liveModeledPoints[i].x - m_liveModeledPoints[i - 1].x,
                                m_liveModeledPoints[i].y - m_liveModeledPoints[i - 1].y);
        if (strokeLen >= 1e-4) {
            break;
        }
    }

    if (m_liveModeledPoints.empty()) {
        // Tremor filter swallowed the stationary tap: synthesize a dot at touchdown origin
        StylusInputPoint pt = m_activeStrokePoints.front();
        float tapPressure = (pt.pressure >= 0.0f) ? pt.pressure : 0.65f;
        float dynamicWidth = EvaluateTapWidth(tapPressure, pt.tiltX, pt.tiltY);
        m_liveModeledPoints.push_back(StrokePoint(pt.x, pt.y, dynamicWidth * 0.5f, tapPressure, pt.tiltX, pt.tiltY));
    } else if (strokeLen < 1e-4) {
        // Google modeler flushed multiple coincident points for a stationary tap; collapse to a single point
        m_liveModeledPoints.resize(1);
    }
}

void InkEngine::ApplyTailTaper(std::vector<StrokePoint>& points) const {
    if (m_config.tailTaperLengthMm <= 0.0f || points.size() < 2) return;

    float minRadius = m_activeStyle.baseWidth * std::min(m_config.minWidthMultiplier, m_config.maxWidthMultiplier) * 0.5f;
    minRadius = std::max(0.001f, minRadius);
    const double taperTargetLen = static_cast<double>(m_config.tailTaperLengthMm);
    const float taperPower = (m_config.tailTaperPower > 0.01f) ? m_config.tailTaperPower : 1.0f;

    const size_t ptCount = points.size();
    double d = 0.0;
    float origTipR = points.back().r;
    points.back().r = std::min(origTipR, minRadius);
    points.back().pressure = 0.0f;

    for (size_t i = ptCount - 1; i > 0; --i) {
        double segLen = std::hypot(points[i].x - points[i - 1].x,
                                   points[i].y - points[i - 1].y);
        d += segLen;
        if (d >= taperTargetLen) {
            break;
        }
        float u = static_cast<float>(std::clamp(d / taperTargetLen, 0.0, 1.0));
        float factor = (taperPower == 1.0f) ? u : std::pow(u, taperPower);
        auto& pt = points[i - 1];
        if (pt.r > minRadius) {
            pt.r = minRadius + (pt.r - minRadius) * factor;
        }
        pt.pressure *= factor;
    }
}

std::vector<Segment1D> InkEngine::BuildCollisionSegments(const std::vector<StrokePoint>& points) const {
    std::vector<Segment1D> segments;
    if (points.size() == 1) {
        // Single-point tap: create a micro-segment because zero-length lines have undefined normals and fail collision checks
        StrokePoint p0 = points[0];
        Point2D pt1(p0.x + 0.001, p0.y); // tiny 1-micron offset gives a valid bounding box and direction for R-Tree & eraser
        Segment1D seg;
        seg.p0 = p0;
        seg.p1 = pt1;
        seg.width = p0.r * 2.0f;
        segments.push_back(seg);
    } else if (points.size() >= 2) {
        // Multi-point stroke: link each consecutive pair of modeled points into a linear collision capsule
        segments.reserve(points.size() - 1);
        for (size_t i = 1; i < points.size(); ++i) {
            Segment1D seg;
            seg.p0 = points[i - 1];
            seg.p1 = points[i];
            seg.width = points[i].r * 2.0f;
            segments.push_back(seg);
        }
    }
    return segments;
}

FinishedStrokeData InkEngine::FinishStroke() {
    // Fast return if no stroke was active
    if (!m_isStrokeActive) return {};

    // Lazy initialization safety check
    if (!m_googleModeler) {
        m_googleModeler = std::make_unique<sm::StrokeModeler>();
        UpdatePhysicsParams();
    }

    // Stage 1: Send Pen-Up (kUp) event to Google Modeler to flush lingering spring inertia
    FlushPhysicsOnPenUp();

    // Stage 2: Handle single-tap / click gestures (user touched down and lifted without lateral movement)
    HandleSingleTapEdgeCase();

    // Stage 3: Optional Tail Taper (smoothly thins out the end of the stroke on pen lift)
    ApplyTailTaper(m_liveModeledPoints);

    // Stage 4: Post-processing simplification (decimates redundant collinear vertices while preserving curve roundness)
    if (m_config.enablePostProcessSimplification && m_liveModeledPoints.size() > 2) {
        m_liveModeledPoints = StrokePostProcessor::Simplify(
            m_liveModeledPoints,
            m_config.simplificationToleranceMm,
            m_config.simplificationMaxTurnDeg,
            m_config.simplificationRadiusTolerance);
    }

    // Stage 5: Build collision segments and finalize immutable stroke package
    FinishedStrokeData data;
    data.liveSegments = BuildCollisionSegments(m_liveModeledPoints);
    data.modeledPoints = m_liveModeledPoints;
    data.outlinePath = InkGenerator::GenerateStroke(
        m_liveModeledPoints, m_activeStyle.capType, m_activeStyle.pattern); // builds closed polygon ribbon (CPU path / export)
    InkMeshBuilder::BuildSolidMesh(
        m_liveModeledPoints, m_activeStyle.capType, PackGpuColor(m_activeStyle.color), data.mesh, m_zoomScale);

    // Auto-dump stroke reproducer if geometry defects (self-intersections or retrograde folds) were detected
    if (InkGenerator::IsDebugCaptureEnabled() && InkGenerator::GetLastDebugInfo().hasDefects) {
        InkGenerator::DumpLastStrokeToConsole();
        InkGenerator::DumpDebugSvg("defect_stroke.svg");
    }

    // Stage 6: Reset live state for the next stroke gesture
    CancelStroke();

    return data;
}

void InkEngine::CancelStroke() {
    // Clear in-flight buffers while preserving pre-allocated capacity
    m_activeStrokePoints.clear();
    m_liveModeledPoints.clear();
    m_googleResultsBuffer.clear();
    m_googlePredictionsBuffer.clear();
    m_predictedPointsBuffer.clear();
    m_liveStrokeMesh.clear();
    m_predictedStrokeMesh.clear();
    m_meshDirty = false;
    m_predictionDirty = false;
    m_smoothedWidth = -1.0f;
    m_isStrokeActive = false;
}

const InkMesh& InkEngine::GetLiveMesh() const noexcept {
    if (m_meshDirty) {
        EnsureLiveMesh();
    }
    return m_liveStrokeMesh;
}

const InkMesh& InkEngine::GetPredictedMesh() const noexcept {
    if (m_predictionDirty) {
        EnsurePrediction();
    }
    return m_predictedStrokeMesh;
}

const std::vector<StrokePoint>& InkEngine::GetPredictedPoints() const noexcept {
    if (m_predictionDirty) {
        EnsurePrediction();
    }
    return m_predictedPointsBuffer;
}

void InkEngine::EnsureLiveMesh() const {
    if (!m_meshDirty) return;
    m_meshDirty = false;
    if (m_liveModeledPoints.empty()) {
        m_liveStrokeMesh.clear();
        return;
    }
    InkMeshBuilder::BuildSolidMesh(
        m_liveModeledPoints, m_activeStyle.capType, PackGpuColor(m_activeStyle.color), m_liveStrokeMesh, m_zoomScale);
}

void InkEngine::EnsurePrediction() const {
    if (!m_predictionDirty) return;
    m_predictionDirty = false;
    m_predictedStrokeMesh.clear();
    m_predictedPointsBuffer.clear();

    if (!m_config.google_enable_prediction || m_liveModeledPoints.empty() || !m_googleModeler) {
        return;
    }

    m_googlePredictionsBuffer.clear();
    if (!m_googleModeler->Predict(m_googlePredictionsBuffer).ok() || m_googlePredictionsBuffer.empty()) {
        return;
    }

    float predWidth = m_smoothedWidth;
    m_predictedPointsBuffer.reserve(m_googlePredictionsBuffer.size() + 1);
    m_predictedPointsBuffer.push_back(m_liveModeledPoints.back());

    for (const auto& res : m_googlePredictionsBuffer) {
        StrokePoint pt = ToStrokePoint(res, predWidth);
        predWidth = pt.r * 2.0f;
        m_predictedPointsBuffer.push_back(pt);
    }

    if (m_predictedPointsBuffer.size() >= 2) {
        InkMeshBuilder::BuildSolidMesh(
            m_predictedPointsBuffer, m_activeStyle.capType, PackGpuColor(m_activeStyle.color), m_predictedStrokeMesh, m_zoomScale);
    }
}

float InkEngine::EvaluateTapWidth(float pressure, float tiltX, float tiltY) const noexcept {
    const bool isPencil = (m_activeStyle.pattern == StrokePattern::TexturedPencil);
    float pForEval = m_evalConfig.usePressure ? pressure : -1.0f;
    return StrokeWidthEvaluator::CalculateDynamicWidth(
        m_activeStyle.baseWidth, pForEval, 0.0f, tiltX, tiltY, -1.0f, m_zoomScale, m_evalConfig, isPencil);
}

// -----------------------------------------------------------------------------
// 2. MATHEMATICAL THICKNESS & MODELING
// -----------------------------------------------------------------------------

// translates Google Ink's smoothed Result output back into our StrokePoint struct with pre-resolved radius
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
    float pForEval = m_evalConfig.usePressure ? effectivePressure : -1.0f;
    float vForEval = m_evalConfig.useVelocity ? speed : 0.0f;

    float dynamicWidth = StrokeWidthEvaluator::CalculateDynamicWidthWithTiltRad(
        m_activeStyle.baseWidth, pForEval, vForEval, tiltRad,
        previousWidth, m_zoomScale, m_evalConfig, isPencil);

    return StrokePoint(res.position.x, res.position.y, dynamicWidth * 0.5f, effectivePressure, tiltX, tiltY);
}

void InkEngine::ProcessGoogleResults(const std::vector<sm::Result>& results) {
    if (results.empty()) return;

    // Convert incoming Google batch into StrokePoints, filtering out stationary duplicates (< 1e-4 mm)
    for (const auto& res : results) {
        StrokePoint pt = ToStrokePoint(res, m_smoothedWidth);
        m_smoothedWidth = pt.r * 2.0f;
        if (m_liveModeledPoints.empty() ||
            std::hypot(pt.x - m_liveModeledPoints.back().x, pt.y - m_liveModeledPoints.back().y) >= 1e-4) {
            m_liveModeledPoints.push_back(pt);
        } else {
            // Update radius/pressure/tilt on stationary initial point
            m_liveModeledPoints.back() = pt;
        }
    }

    if (!m_liveModeledPoints.empty()) {
        m_meshDirty = true;
    }
}

// -----------------------------------------------------------------------------
// 3. CONFIGURATION (Public Facade)
// -----------------------------------------------------------------------------

InkingConfig& InkEngine::GetConfig() noexcept {
    return m_config;
}

void InkEngine::SetConfig(const InkingConfig& config) {
    m_config = config;
    UpdateEvalConfig();
    UpdatePhysicsParams();
}

void InkEngine::UpdateEvalConfig() noexcept {
    m_evalConfig = m_config;
    m_evalConfig.usePressure = m_config.usePressure && m_activeStyle.usePressure;
    m_evalConfig.useVelocity = m_config.useVelocity && m_activeStyle.useVelocity;
    m_evalConfig.useTilt     = m_config.useTilt || m_activeStyle.useTilt;
}

void InkEngine::UpdatePhysicsParams() {
    if (!m_googleModeler) {
        return;
    }

    // Configure Google Ink Stroke Modeler physics parameters
    sm::StrokeModelParams params;
    params.position_modeler_params.spring_mass_constant = m_config.google_spring_mass_constant;
    params.position_modeler_params.drag_constant = m_config.google_drag_constant;

    // Speed-based loop contraction mitigation (prevents fast curves/loops from shrinking)
    auto& loopParams = params.position_modeler_params.loop_contraction_mitigation_params;
    loopParams.is_enabled = m_config.google_loop_mitigation_enable;
    if (loopParams.is_enabled) {
        float lowSpeed = std::max(0.0f, m_config.google_loop_speed_lower_bound);
        float highSpeed = std::max(lowSpeed, m_config.google_loop_speed_upper_bound);
        float lowStrength = std::clamp(m_config.google_loop_strength_at_lower_speed, 0.0f, 1.0f);
        float highStrength = std::clamp(m_config.google_loop_strength_at_upper_speed, 0.0f, lowStrength);
        loopParams.speed_lower_bound = lowSpeed;
        loopParams.speed_upper_bound = highSpeed;
        loopParams.interpolation_strength_at_speed_lower_bound = lowStrength;
        loopParams.interpolation_strength_at_speed_upper_bound = highStrength;
        loopParams.min_speed_sampling_window = sm::Duration(std::max(0.0f, m_config.google_loop_min_speed_window_s));
    }

    // Stylus normal projection (required when loop mitigation is enabled)
    params.stylus_state_modeler_params.use_stroke_normal_projection =
        m_config.google_use_stroke_normal_projection || m_config.google_loop_mitigation_enable;

    // Wobble smoother (hand tremor filter)
    params.wobble_smoother_params.is_enabled = m_config.google_wobble_enable;
    params.wobble_smoother_params.timeout = sm::Duration(std::max(0.0f, m_config.google_wobble_timeout_s));
    params.wobble_smoother_params.speed_floor = std::max(0.0f, m_config.google_wobble_speed_floor);
    params.wobble_smoother_params.speed_ceiling =
        std::max(params.wobble_smoother_params.speed_floor, m_config.google_wobble_speed_ceiling);

    // Sampling & rotational velocity upsampling
    params.sampling_params.min_output_rate = std::max(10.0, static_cast<double>(m_config.google_min_output_rate));
    params.sampling_params.end_of_stroke_stopping_distance =
        std::max(0.0001f, m_config.google_end_of_stroke_stopping_distance);
    params.sampling_params.end_of_stroke_max_iterations =
        std::clamp(m_config.google_end_of_stroke_max_iterations, 1, 1000);
    params.sampling_params.max_estimated_angle_to_traverse_per_input =
        m_config.google_enable_angle_upsampling
            ? std::clamp(static_cast<double>(m_config.google_max_estimated_angle_rad), 0.005, 3.10)
            : -1.0;

    // Initialize/re-initialize physics parameters
    (void)m_googleModeler->Reset(params);
}

} // namespace Folio
