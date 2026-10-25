#include "folio_sketch/ink_generator.hpp"
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

// =============================================================================
// INCREMENTAL LIVE INKING SEGMENT APPEND
// =============================================================================

/**
 * @brief Incrementally tessellates and appends only newly added stroke segments in O(1) time.
 *
 * During live inking at 120Hz/240Hz, re-tessellating the full stroke from scratch on every frame
 * results in O(N^2) CPU overhead. AppendRibbonSegments enables real-time sub-millisecond inking by
 * tessellating only the new segment trapezoids [startIdx - 1 .. pts.size() - 1] and appending
 * them directly onto the active GPU/raster canvas path.
 *
 * @param path Active path being grown during the gesture.
 * @param pts Full buffer of stroke points.
 * @param startIdx Index of the first new point appended in this frame.
 * @param capType Active nib profile.
 * @param pattern Stroke pattern.
 */
void InkGenerator::AppendRibbonSegments(InkPath& path,
                                        const std::vector<StrokePoint>& pts,
                                        size_t startIdx,
                                        CapType capType,
                                        StrokePattern pattern) {
    if (pts.empty() || startIdx >= pts.size()) return;

    // Textured pencil uses round disc caps to simulate graphite lead
    const CapType effectiveCap = (pattern == StrokePattern::TexturedPencil) ? CapType::Round : capType;

    // Helper: emits a round circle or polygon nib shape at joint vertices to prevent cracks
    auto emitCap = [&](const StrokePoint& p) {
        if (p.r <= 0.002f) return;
        if (effectiveCap == CapType::Round) {
            path.addCircle(p.x, p.y, p.r);
        } else {
            auto nibVerts = Folio::InkInternal::GetNibVertices(effectiveCap, p.r);
            if (!nibVerts.empty()) {
                path.move_to(p.x + nibVerts[0].x, p.y + nibVerts[0].y);
                for (size_t k = 1; k < nibVerts.size(); ++k) {
                    path.line_to(p.x + nibVerts[k].x, p.y + nibVerts[k].y);
                }
                path.close();
            }
        }
    };

    // If starting a brand new stroke, emit the initial cap at point 0
    if (startIdx == 0) {
        emitCap(Folio::InkInternal::ResolveEnvelopePoint(pts[0], pattern));
        startIdx = 1;
    }

    // Tessellate each newly added segment as a quad/trapezoid connecting p0 and p1
    for (size_t i = startIdx; i < pts.size(); ++i) {
        StrokePoint p0 = Folio::InkInternal::ResolveEnvelopePoint(pts[i - 1], pattern);
        StrokePoint p1 = Folio::InkInternal::ResolveEnvelopePoint(pts[i],     pattern);

        emitCap(p1);

        if (p0.r <= 0.002f && p1.r <= 0.002f) continue;
        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double len = std::hypot(dx, dy);
        if (len < 1e-6) continue;

        // Normal vector perpendicular to the segment (p0 -> p1)
        double nx =  dy / len;
        double ny = -dx / len;

        // Compute left and right rail offsets at both ends of the segment
        Point2D offL0, offR0, offL1, offR1;
        Folio::InkInternal::GetRibbonOffsets(effectiveCap, std::max(0.0f, p0.r), nx, ny, offL0, offR0);
        Folio::InkInternal::GetRibbonOffsets(effectiveCap, std::max(0.0f, p1.r), nx, ny, offL1, offR1);

        // Append the closed quad (p0_left -> p0_right -> p1_right -> p1_left)
        path.move_to(p0.x + offL0.x, p0.y + offL0.y);
        path.line_to(p0.x + offR0.x, p0.y + offR0.y);
        path.line_to(p1.x + offR1.x, p1.y + offR1.y);
        path.line_to(p1.x + offL1.x, p1.y + offL1.y);
        path.close();
    }
}

} // namespace Folio


