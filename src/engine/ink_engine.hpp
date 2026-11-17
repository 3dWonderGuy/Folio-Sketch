#pragma once
/**
 * =========================================================================================
 * @file include/folio_sketch/engine/ink_engine.hpp
 * @brief Core Vector Inking Engine & Stroke Modeler Orchestrator
 * =========================================================================================
 *
 * Single facade orchestrating the stylus input lifecycle:
 * 
 * 1. LIFECYCLE EXECUTION ORDER:
 *    a. BeginStroke(...)    : Stylus touch-down. Initializes the engine
 *    b. IngestSamples(...)  : Stylus move (batch). Feeds all sub-frame hardware digitizer. (For Pen)
 *       OR AppendPoint(...) : Stylus move (discrete). Feeds a single point. Preferred for mouse.
 *    c. Render / VSync Tick : The renderer requests LiveCapsuleSpan() / PredictedCapsuleSpan().
 *                             Geometry & Kalman prediction are evaluated lazily on demand.
 *    d. FinishStroke()      : Stylus touch-up. Runs stroke simplification/smoothing and
 *                             returns FinishedStrokeData for layer persistence.
 *    e. CancelStroke()      : Discards in-flight stroke data without output.
 *
 * 2. IngestSamples vs AppendPoint:
 *    - IngestSamples: High-throughput batch API. Captures high-frequency digitizer samples
 *      (240Hz+) without per-point caller overhead.
 *    - AppendPoint: Single-point API for discrete events, tests, and backwards compatibility.
 */

#include <vector>
#include <memory>
#include <span>

#include "pipelines/vector/ink_path.hpp"
#include "pipelines/vector/ink_generator.hpp"
#include "core/ink_stroke_data.hpp"
#include "core/ink_config.hpp"
#include "pipelines/capsule/ink_capsule.hpp"
#include "pipelines/raster/ink_raster.hpp"

namespace ink::stroke_model {
    struct Result;
}

namespace Folio {

class InkPhysicsBridge;

/**
 * @struct FinishedStrokeData
 * @brief Completed inking stroke container encapsulating collision segments and modeled centerline.
 *        Rendering geometry (capsules, vector outlines, raster dabs) is built on demand.
 */
struct FinishedStrokeData {
    std::vector<Segment1D>       liveSegments;
    std::vector<StrokePoint>     rawModeledPoints; ///< Modeler points before simplification / Gaussian smoothing
    std::vector<StrokePoint>     modeledPoints;    ///< Post-processed centerline points

    FinishedStrokeData() = default;
};

class InkEngine {
public:
    InkEngine();
    ~InkEngine();

    // Prevent accidental copying or slicing of live engine state and physics solver buffers
    InkEngine(const InkEngine&) = delete;            // Non-copyable
    InkEngine& operator=(const InkEngine&) = delete; // Non-copy-assignable
    InkEngine(InkEngine&&) = delete;                 // Non-movable
    InkEngine& operator=(InkEngine&&) = delete;      // Non-move-assignable

    // -------------------------------------------------------------------------
    // 1. INKING LIFECYCLE
    // -------------------------------------------------------------------------

    /**
     * @brief Initiates a new live vector stroke gesture.
     *
     * @param worldX Initial stylus X position in physical millimeters.
     * @param worldY Initial stylus Y position in physical millimeters.
     * @param pressure Initial contact pressure [0.0, 1.0].
     * @param timeSec Monotonic event timestamp in seconds.
     * @param style Active stroke style, nib dimensions, pattern, and pigment.
     * @param zoomScale Current viewport zoom factor.
     * @param tiltX Stylus tilt angle along X axis in radians.
     * @param tiltY Stylus tilt angle along Y axis in radians.
     */
    void BeginStroke(double worldX, double worldY, float pressure, double timeSec,
                     const StrokeStyle& style, float zoomScale = 1.0f,
                     float tiltX = 0.0f, float tiltY = 0.0f);

    /**
     * @brief Ingests continuous motion telemetry from stylus or mouse.
     *
     * @param worldX Current stylus X position in millimeters.
     * @param worldY Current stylus Y position in millimeters.
     * @param pressure Current contact pressure [0.0, 1.0].
     * @param timeSec Monotonic event timestamp in seconds.
     * @param tiltX Current stylus tilt X.
     * @param tiltY Current stylus tilt Y.
     */
    void AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                     float tiltX = 0.0f, float tiltY = 0.0f);

    /**
     * @brief Packet-oriented entry point: begins a stroke from a raw digitizer packet.
     */
    void BeginStroke(const StylusInputPoint& sample, const StrokeStyle& style, float zoomScale = 1.0f) {
        BeginStroke(sample.x, sample.y, sample.pressure, sample.TimeSec(), style, zoomScale,
                    sample.tiltX, sample.tiltY);
    }

    /**
     * @brief Packet-oriented entry point: ingests a whole batch of raw digitizer packets
     *        (e.g. an Android MotionEvent's historical samples plus its current sample).
     *
     * Every sample is fed to the modeler in order, so no hardware sample is dropped. Capsule
     * and prediction geometry is rebuilt lazily once per batch, not once per sample.
     */
    void IngestSamples(std::span<const StylusInputPoint> samples) {
        for (const StylusInputPoint& s : samples) {
            AppendPoint(s.x, s.y, s.pressure, s.TimeSec(), s.tiltX, s.tiltY);
        }
    }

    /**
     * @brief Finalizes the active stroke gesture upon pointer lift.
     * @return FinishedStrokeData The completed stroke package ready for layer commitment.
     */
    FinishedStrokeData FinishStroke();

    /**
     * @brief Discards the active in-flight stroke without generating output.
     */
    void CancelStroke();

    // -------------------------------------------------------------------------
    // 2. STATE & GEOMETRY ACCESSORS
    // -------------------------------------------------------------------------

    [[nodiscard]] bool IsStrokeActive() const noexcept { return m_isStrokeActive; }             // is pen down?
    [[nodiscard]] const std::vector<StrokePoint>& GetLiveModeledPoints() const noexcept { return m_liveModeledPoints; } // current smoothed centerline
    [[nodiscard]] const std::vector<StrokePoint>& GetPredictedPoints() const noexcept;          // current predicted points
    [[nodiscard]] const std::vector<CapsuleInstance>& GetLiveCapsules() const noexcept;         // current live capsule segments
    [[nodiscard]] const std::vector<CapsuleInstance>& GetPredictedCapsules() const noexcept;    // current predicted capsule segments

    /// Zero-copy POD views handed straight to a GPU backend (see folio_backends/).
    [[nodiscard]] std::span<const CapsuleInstance> LiveCapsuleSpan() const noexcept { return GetLiveCapsules(); }
    [[nodiscard]] std::span<const CapsuleInstance> PredictedCapsuleSpan() const noexcept { return GetPredictedCapsules(); }
    [[nodiscard]] const StrokeStyle& GetActiveStyle() const noexcept { return m_activeStyle; } // current stroke style

    // -------------------------------------------------------------------------
    // 3. CONFIGURATION MANAGEMENT
    // -------------------------------------------------------------------------

    [[nodiscard]] InkingConfig& GetConfig() noexcept { return m_config; }
    void SetConfig(const InkingConfig& config);
    void UpdatePhysicsParams();

    // -------------------------------------------------------------------------
    // 4. CENTRAL AUTHORITIES
    // -------------------------------------------------------------------------

    /**
     * @brief Centralized stroke outline computing authority (for SVG / PDF / Clipboard).
     */
    static InkPath BuildStrokeOutline(const std::vector<StrokePoint>& points,
                                      CapType capType = CapType::Round,
                                      StrokePattern pattern = StrokePattern::Solid,
                                      bool isStandardShape = false) {
        return InkGenerator::GenerateStroke(points, capType, pattern, isStandardShape);
    }

    /**
     * @brief Centralized GPU Capsule SDF segment instance computing authority.
     */
    static void BuildStrokeCapsules(std::span<const StrokePoint> points,
                                    uint32_t premulColorRgba,
                                    std::vector<CapsuleInstance>& outCapsules) {
        CapsuleBuilder::BuildCapsuleInstances(points, premulColorRgba, outCapsules);
    }

    /**
     * @brief Centralized Procreate-style Raster Dab & Smudge instance computing authority.
     */
    static void BuildStrokeRasterDabs(std::span<const StrokePoint> points,
                                      ColorRgba baseColor,
                                      const RasterBrushConfig& config,
                                      std::vector<RasterDabInstance>& outDabs) {
        RasterDabBuilder::BuildDabInstances(points, baseColor, config, outDabs);
    }

private:
    /// Maps raw Google Modeler physics result to calibrated, dynamic StrokePoint
    StrokePoint ToStrokePoint(const ink::stroke_model::Result& res) const;

    /// Ingests new physics samples into live centerline points
    void ProcessPhysicsResults(const std::vector<ink::stroke_model::Result>& results);

    /// Computes dab diameter for stationary tap/dot gestures
    [[nodiscard]] float EvaluateTapWidth(float pressure) const noexcept;

    /// Catches m_liveCapsules up with the newest modeled points when the GPU asks for them
    void SyncLiveCapsules() const;

    /// Extrapolates future centerline points lazily on demand
    void ComputePredictedPoints() const;

    /// Builds predictive GPU capsules + alpha fade from predicted points when the GPU asks for them
    void BuildPredictedCapsules() const;

    /// Caches zoom-scaled configuration parameters for tight loop evaluation
    void UpdateEvalConfig() noexcept;

    // Gesture State
    bool m_isStrokeActive = false;
    StrokeStyle m_activeStyle;
    InkingConfig m_config;
    InkingConfig m_evalConfig;
    float m_zoomScale = 1.0f;

    // Dynamics Temporal State (Acceleration filtering & Corner compensation)
    float m_filteredSpeedMmPerSec = 0.0f;
    double m_lastPointTimeSec = 0.0;
    float m_lastHeadingRad = 0.0f;
    bool m_hasHeading = false;
    float m_currentTurnAngleRad = 0.0f;
    double m_lastModeledTimeSec = 0.0;

    // Geometry & Result Buffers
    std::vector<StylusInputPoint> m_activeStrokePoints;
    std::vector<StrokePoint> m_liveModeledPoints;
    std::vector<ink::stroke_model::Result> m_physicsResultsBuffer;
    mutable std::vector<ink::stroke_model::Result> m_predictionResultsBuffer;
    mutable std::vector<StrokePoint> m_predictedPointsBuffer;

    mutable std::vector<CapsuleInstance> m_liveCapsules;
    mutable std::vector<CapsuleInstance> m_predictedCapsules;
    mutable size_t m_validLiveSegments = 0; ///< Leading live capsules that are final (incremental build cursor)
    mutable bool m_capsulesDirty = false;   ///< Private lazy-cache guard for LiveCapsuleSpan() only
    mutable bool m_predictionDirty = false;
    mutable bool m_predictedCapsulesDirty = false; ///< Private lazy-cache guard for PredictedCapsuleSpan() only

    // Physics Engine Bridge
    std::unique_ptr<InkPhysicsBridge> m_physicsBridge;
};

} // namespace Folio
