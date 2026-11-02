#pragma once
/**
 * =========================================================================================
 * @file include/folio_sketch/engine/ink_engine.hpp
 * @brief Core Vector Inking Engine & Stroke Modeler Orchestrator
 * =========================================================================================
 *
 * Single facade orchestrating:
 * - Stylus input lifecycle (BeginStroke, AppendPoint, FinishStroke, CancelStroke)
 * - Dynamic line thickness evaluation (pressure gamma, velocity thinning, tilt angle)
 * - Live GPU Capsule SDF streaming (instantaneous O(N) segment generation)
 * - Forward Kalman prediction to eliminate perceived glass-to-display latency
 * - 2D Vector Path outline generation (InkPath) for SVG, PDF, and clipboard export
 */

#include <vector>
#include <memory>
#include <span>

#include "folio_sketch/vector/ink_path.hpp"
#include "folio_sketch/vector/ink_generator.hpp"
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/core/ink_config.hpp"
#include "folio_sketch/gpu/ink_mesh.hpp"

namespace ink::stroke_model {
    struct Result;
}

namespace Folio {

class InkPhysicsBridge;

/**
 * @struct FinishedStrokeData
 * @brief Completed inking stroke container encapsulating collision segments, modeled centerline,
 *        GPU capsule instances, outline polygon, and legacy GPU mesh.
 */
struct FinishedStrokeData {
    std::vector<Segment1D>       liveSegments;
    std::vector<StrokePoint>     modeledPoints;
    std::vector<CapsuleInstance> capsules;
    InkPath                      outlinePath;
    InkMesh                      mesh;

    FinishedStrokeData() = default;
};

class InkEngine {
public:
    InkEngine();
    ~InkEngine();

    InkEngine(const InkEngine&) = delete;
    InkEngine& operator=(const InkEngine&) = delete;
    InkEngine(InkEngine&&) = delete;
    InkEngine& operator=(InkEngine&&) = delete;

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

    [[nodiscard]] bool IsStrokeActive() const noexcept { return m_isStrokeActive; }
    [[nodiscard]] const std::vector<StrokePoint>& GetLiveModeledPoints() const noexcept { return m_liveModeledPoints; }
    [[nodiscard]] const std::vector<StrokePoint>& GetPredictedPoints() const noexcept;
    [[nodiscard]] const std::vector<CapsuleInstance>& GetLiveCapsules() const noexcept;
    [[nodiscard]] const std::vector<CapsuleInstance>& GetPredictedCapsules() const noexcept;
    [[nodiscard]] const StrokeStyle& GetActiveStyle() const noexcept { return m_activeStyle; }

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

private:
    StrokePoint ToStrokePoint(const ink::stroke_model::Result& res, float previousWidth = -1.0f) const;
    void ProcessPhysicsResults(const std::vector<ink::stroke_model::Result>& results);
    [[nodiscard]] float EvaluateTapWidth(float pressure, float tiltX, float tiltY) const noexcept;

    void EnsureLiveCapsules() const;
    void EnsurePrediction() const;
    void UpdateEvalConfig() noexcept;

    // Gesture State
    bool m_isStrokeActive = false;
    StrokeStyle m_activeStyle;
    InkingConfig m_config;
    InkingConfig m_evalConfig;
    float m_zoomScale = 1.0f;
    float m_smoothedWidth = -1.0f;

    // Geometry & Result Buffers
    std::vector<StylusInputPoint> m_activeStrokePoints;
    std::vector<StrokePoint> m_liveModeledPoints;
    std::vector<ink::stroke_model::Result> m_physicsResultsBuffer;
    mutable std::vector<ink::stroke_model::Result> m_predictionResultsBuffer;
    mutable std::vector<StrokePoint> m_predictedPointsBuffer;

    mutable std::vector<CapsuleInstance> m_liveCapsules;
    mutable std::vector<CapsuleInstance> m_predictedCapsules;
    mutable bool m_capsulesDirty = false;
    mutable bool m_predictionDirty = false;

    // Physics Engine Bridge
    std::unique_ptr<InkPhysicsBridge> m_physicsBridge;
};

} // namespace Folio
