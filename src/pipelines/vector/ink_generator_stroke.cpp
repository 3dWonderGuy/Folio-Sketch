#include "pipelines/vector/ink_generator.hpp"
#include "ink_generator_internal.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

namespace Folio {
namespace InkInternal {

/**
 * @brief Resolves the outer contact geometry of a stroke point for textured graphite pencil.
 *
 * When using a physical pencil, tilting the stylus shifts the contact patch along the tilt direction
 * and broadens the stroke width due to the conic shape of the graphite core.
 * This function applies the directional displacement offset (rTiltExtra) to simulate real pencil lead
 * contact physics.
 *
 * @param pt Input modeled stroke point with tilt telemetry.
 * @param pattern Active stroke pattern.
 * @return Point with translated (x, y) coordinates if pencil pattern is active; unchanged otherwise.
 */
StrokePoint ResolveEnvelopePoint(const StrokePoint& pt, StrokePattern pattern) noexcept {
    if (pattern != StrokePattern::TexturedPencil) {
        return pt;
    }
    // Compute total tilt magnitude in radians from normal
    float tiltMag = std::hypot(pt.tiltX, pt.tiltY);
    // Unit vector along the stylus tilt direction
    double ux = (tiltMag > 1e-4f) ? (pt.tiltX / tiltMag) : 0.0;
    double uy = (tiltMag > 1e-4f) ? (pt.tiltY / tiltMag) : 0.0;

    // Below 0.22 radians (~12.6°), the pen writes on its sharp conical apex.
    // Above 0.22 radians, the pencil body tilts onto its side, increasing contact area.
    float tiltFactor = 1.0f;
    if (tiltMag > 0.22f) {
        float t = std::clamp((tiltMag - 0.22f) / (1.05f - 0.22f), 0.0f, 1.0f);
        // Smoothstep interpolation (Hermite polynomial)
        float smoothT = t * t * (3.0f - 2.0f * t);
        tiltFactor = 1.0f + smoothT * 1.8f;
    }
    float rTip = pt.r / tiltFactor;
    float rTiltExtra = std::max(0.0f, pt.r - rTip);
    return StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, pt.r, pt.pressure, pt.tiltX, pt.tiltY);
}

} // namespace InkInternal

// =============================================================================
// OPEN STROKE BUILDER
// =============================================================================

/**
 * @brief Top-level open stroke dispatcher.
 *
 * Handles single-point taps, continuous solid ribbons, and discontinuous pattern generation
 * (dashed, dotted, dash-dot, textured pencil).
 *
 * @param centerline Ordered list of smoothed stroke points with radii.
 * @param capType Nib profile for endpoints (Round, Chisel, Square).
 * @param pattern Stroke pattern.
 * @return Completed 2D closed polygon path representing the stroke.
 */
InkPath InkGenerator::BuildStroke(const std::vector<StrokePoint>& centerline,
                                  CapType capType,
                                  StrokePattern pattern) {
    if (centerline.empty()) return InkPath{};
    // Single tap (zero motion distance): trace the stationary nib footprint
    if (centerline.size() == 1) return TraceCap(centerline[0], capType);

    // Continuous solid line: routes to ribbon generator (miter corner solver)
    if (pattern == StrokePattern::Solid) {
        return BuildSolidRibbon(centerline, capType);
    }
    // Discontinuous patterns (Dotted, Dashed, DashDot, TexturedPencil)
    return ApplyDiscontinuousPattern(centerline, pattern);
}

} // namespace Folio


