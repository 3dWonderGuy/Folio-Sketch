#pragma once
/**
 * =========================================================================================
 * @file src/engine/ink_physics_bridge.hpp
 * @brief Physics Engine Bridge: Google Ink Stroke Modeler Integration
 * =========================================================================================
 *
 * Encapsulates the Google Ink Stroke Modeler spring-mass-damper physics engine.
 * Converts raw hardware digitizer telemetry (position, pressure, tilt) into physical
 * inputs, applies configuration parameters (mass, drag, wobble filters), and computes
 * smoothed centerline points and forward Kalman predictions.
 */

#include <vector>
#include <memory>
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/core/ink_config.hpp"

// Forward declaration of Google Ink Stroke Modeler classes
namespace ink::stroke_model {
    class StrokeModeler;
    struct Result;
}

namespace Folio {

class InkPhysicsBridge {
public:
    InkPhysicsBridge();
    ~InkPhysicsBridge();

    InkPhysicsBridge(const InkPhysicsBridge&) = delete;
    InkPhysicsBridge& operator=(const InkPhysicsBridge&) = delete;

    /**
     * @brief Pushes active InkingConfig settings into Google Modeler physics parameters.
     */
    void UpdateConfig(const InkingConfig& config);

    /**
     * @brief Resets modeler internal state for a new stroke while retaining active physics parameters.
     */
    void Reset();

    /**
     * @brief Seeds the initial touchdown sample (kDown) into the physics solver.
     */
    bool ProcessTouchDown(const StylusInputPoint& pt, std::vector<ink::stroke_model::Result>& outResults);

    /**
     * @brief Ingests continuous motion (kMove) and returns physically-modeled results.
     */
    bool ProcessMotion(const StylusInputPoint& pt, std::vector<ink::stroke_model::Result>& outResults);

    /**
     * @brief Flushes spring inertia on pen-up (kUp) to settle the tip at the lift position.
     */
    bool ProcessTouchUp(const StylusInputPoint& pt, std::vector<ink::stroke_model::Result>& outResults);

    /**
     * @brief Computes forward Kalman prediction points to hide stylus-to-display latency.
     */
    bool Predict(std::vector<ink::stroke_model::Result>& outPredictions);

private:
    std::unique_ptr<ink::stroke_model::StrokeModeler> m_modeler;
};

} // namespace Folio
