/**
 * =========================================================================================
 * @file src/engine/ink_stroke_finisher.cpp
 * @brief Stroke Finalization, Edge-Case Handling, and Collision Packaging
 * =========================================================================================
 */

#include "ink_stroke_finisher.hpp"
#include <algorithm>
#include <cmath>

namespace Folio {

/**
 * @brief Ensures a tap/click produces a visible dot if the tremor filter swallowed lateral motion,
 *        or collapses multiple coincident points from a stationary tap down to a single point.
 *
 * @param rawInputs The raw stylus inputs received from the digitizer.
 * @param tapWidth The base diameter in mm to assign to synthesized tap dots.
 * @param inoutModeledPoints The modeled/filtered points to be checked and adjusted in-place.
 */
void InkStrokeFinisher::ResolveSingleTapEdgeCase(
    const std::vector<StylusInputPoint>& rawInputs,
    float tapWidth,
    std::vector<StrokePoint>& inoutModeledPoints)
{
    if (rawInputs.empty()) return;

    // Calculate the total arc length of the stroke to determine if any meaningful motion occurred.
    // Early break once length passes 0.1 microns (1e-4 mm) to avoid unnecessary computation for real strokes.
    double strokeLen = 0.0;
    for (size_t i = 1; i < inoutModeledPoints.size(); ++i) {
        strokeLen += std::hypot(inoutModeledPoints[i].x - inoutModeledPoints[i - 1].x,
                                inoutModeledPoints[i].y - inoutModeledPoints[i - 1].y);
        if (strokeLen >= 1e-4) {
            break;
        }
    }

    if (inoutModeledPoints.empty()) {
        // Case 1: The tremor/noise filter rejected all inputs because there was no delta movement.
        // We synthesize a single point at the initial touchdown location to represent a dot or period.
        const auto& pt = rawInputs.front();
        float tapPressure = (pt.pressure >= 0.0f) ? pt.pressure : 0.65f;
        inoutModeledPoints.push_back(StrokePoint(pt.x, pt.y, tapWidth * 0.5f, tapPressure, pt.tiltX, pt.tiltY));
    } else if (strokeLen < 1e-4) {
        // Case 2: The modeler emitted multiple points, but all were clustered at the exact same spot.
        // Collapse them to a single point to avoid zero-length degenerate segments.
        inoutModeledPoints.resize(1);
    }
}

/**
 * @brief Applies smooth geometric tapering to the tail of the stroke upon stylus liftoff.
 *
 * Simulates the natural fading of ink and pressure as the pen tip lifts off the writing surface.
 * Walks backwards from the tip over tailTaperLengthMm, shrinking radius and pressure along a power curve.
 *
 * @param config Active inking configuration containing dynamics and taper parameters.
 * @param style Style of the stroke defining base width and properties.
 * @param inoutModeledPoints The modeled stroke points to modify in-place.
 */
void InkStrokeFinisher::ApplyTailTaper(
    const InkingConfig& config,
    const StrokeStyle& style,
    std::vector<StrokePoint>& inoutModeledPoints)
{
    // Skip tapering if disabled in config or if there are fewer than 2 points
    if (config.dynamics.tailTaperLengthMm <= 0.0f || inoutModeledPoints.size() < 2) return;

    // Compute the minimum allowable radius floor for the tapered tip
    float minRadius = style.baseWidth * std::min(config.dynamics.minWidthMultiplier, config.dynamics.maxWidthMultiplier) * 0.5f;
    minRadius = std::max(0.001f, minRadius);
    const double taperTargetLen = static_cast<double>(config.dynamics.tailTaperLengthMm);
    const float taperPower = (config.dynamics.tailTaperPower > 0.01f) ? config.dynamics.tailTaperPower : 1.0f;

    const size_t ptCount = inoutModeledPoints.size();
    double d = 0.0;

    // Force the very last point to minimum radius and zero pressure (complete liftoff)
    float origTipR = inoutModeledPoints.back().r;
    inoutModeledPoints.back().r = std::min(origTipR, minRadius);
    inoutModeledPoints.back().pressure = 0.0f;

    // Walk backwards along the tail of the stroke, scaling radius and pressure
    for (size_t i = ptCount - 1; i > 0; --i) {
        double segLen = std::hypot(inoutModeledPoints[i].x - inoutModeledPoints[i - 1].x,
                                   inoutModeledPoints[i].y - inoutModeledPoints[i - 1].y);
        d += segLen;
        if (d >= taperTargetLen) {
            // Reached the target taper distance; stop tapering earlier points
            break;
        }

        // Normalized distance parameter u in [0, 1] from liftoff tip (0 = tip, 1 = start of taper)
        float u = static_cast<float>(std::clamp(d / taperTargetLen, 0.0, 1.0));
        float factor = (taperPower == 1.0f) ? u : std::pow(u, taperPower);

        auto& pt = inoutModeledPoints[i - 1];
        if (pt.r > minRadius) {
            pt.r = minRadius + (pt.r - minRadius) * factor;
        }
        pt.pressure *= factor;
    }
}

/**
 * @brief Constructs a list of 1D capsule segments from modeled stroke points for collision and hit-testing.
 *
 * @param modeledPoints The modeled points making up the final stroke.
 * @return std::vector<Segment1D> Vector of segments representing the stroke's collision spine.
 */
std::vector<Segment1D> InkStrokeFinisher::BuildCollisionSegments(
    const std::vector<StrokePoint>& modeledPoints)
{
    std::vector<Segment1D> segments;

    if (modeledPoints.size() == 1) {
        // Single-point tap: create a tiny micro-segment (0.001 mm offset in X).
        // Standard ray/capsule distance formulas require two distinct endpoints to establish a vector direction.
        const auto& p0 = modeledPoints[0];
        Point2D pt1(p0.x + 0.001, p0.y);
        Segment1D seg;
        seg.p0 = p0;
        seg.p1 = pt1;
        seg.width = p0.r * 2.0f;
        segments.push_back(seg);
    } else if (modeledPoints.size() >= 2) {
        // Multi-point stroke: link each consecutive pair of modeled points into a collision segment
        segments.reserve(modeledPoints.size() - 1);
        for (size_t i = 1; i < modeledPoints.size(); ++i) {
            Segment1D seg;
            seg.p0 = modeledPoints[i - 1];
            seg.p1 = modeledPoints[i];
            seg.width = modeledPoints[i].r * 2.0f;
            segments.push_back(seg);
        }
    }

    return segments;
}

} // namespace Folio
