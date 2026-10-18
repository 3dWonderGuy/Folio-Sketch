#include "folio_sketch/ink_generator.hpp"
#include "ink_generator_internal.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

namespace Folio {
namespace InkInternal {

StrokePoint ResolveEnvelopePoint(const StrokePoint& pt, StrokePattern pattern) noexcept {
    if (pattern != StrokePattern::TexturedPencil) {
        return pt;
    }
    float tiltMag = std::hypot(pt.tiltX, pt.tiltY);
    double ux = (tiltMag > 1e-4f) ? (pt.tiltX / tiltMag) : 0.0;
    double uy = (tiltMag > 1e-4f) ? (pt.tiltY / tiltMag) : 0.0;

    float tiltFactor = 1.0f;
    if (tiltMag > 0.22f) {
        float t = std::clamp((tiltMag - 0.22f) / (1.05f - 0.22f), 0.0f, 1.0f);
        float smoothT = t * t * (3.0f - 2.0f * t);
        tiltFactor = 1.0f + smoothT * 1.8f;
    }
    float rTip = pt.r / tiltFactor;
    float rTiltExtra = std::max(0.0f, pt.r - rTip);
    return StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, pt.r, pt.pressure, pt.tiltX, pt.tiltY);
}

} // namespace InkInternal
} // namespace Folio

// =============================================================================
// OPEN STROKE BUILDER
// =============================================================================

BLPath InkGenerator::BuildStroke(const std::vector<StrokePoint>& centerline,
                                 CapType capType,
                                 StrokePattern pattern) {
    if (centerline.empty()) return BLPath{};
    if (centerline.size() == 1) return TraceCap(centerline[0], capType);

    if (pattern == StrokePattern::Solid) {
        return BuildSolidRibbon(centerline, capType);
    }
    return ApplyDiscontinuousPattern(centerline, pattern);
}

// =============================================================================
// INCREMENTAL LIVE INKING SEGMENT APPEND
// =============================================================================

void InkGenerator::AppendRibbonSegments(BLPath& path,
                                        const std::vector<StrokePoint>& pts,
                                        size_t startIdx,
                                        CapType capType,
                                        StrokePattern pattern) {
    if (pts.empty() || startIdx >= pts.size()) return;

    const CapType effectiveCap = (pattern == StrokePattern::TexturedPencil) ? CapType::Round : capType;

    auto emitCap = [&](const StrokePoint& p) {
        if (p.r <= 0.002f) return;
        if (effectiveCap == CapType::Round) {
            path.add_circle(BLCircle(p.x, p.y, p.r), BL_GEOMETRY_DIRECTION_CW);
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

    if (startIdx == 0) {
        emitCap(Folio::InkInternal::ResolveEnvelopePoint(pts[0], pattern));
        startIdx = 1;
    }

    for (size_t i = startIdx; i < pts.size(); ++i) {
        StrokePoint p0 = Folio::InkInternal::ResolveEnvelopePoint(pts[i - 1], pattern);
        StrokePoint p1 = Folio::InkInternal::ResolveEnvelopePoint(pts[i],     pattern);

        emitCap(p1);

        if (p0.r <= 0.002f && p1.r <= 0.002f) continue;
        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double len = std::hypot(dx, dy);
        if (len < 1e-6) continue;

        double nx =  dy / len;
        double ny = -dx / len;

        Point2D offL0, offR0, offL1, offR1;
        Folio::InkInternal::GetRibbonOffsets(effectiveCap, std::max(0.0f, p0.r), nx, ny, offL0, offR0);
        Folio::InkInternal::GetRibbonOffsets(effectiveCap, std::max(0.0f, p1.r), nx, ny, offL1, offR1);

        path.move_to(p0.x + offL0.x, p0.y + offL0.y);
        path.line_to(p0.x + offR0.x, p0.y + offR0.y);
        path.line_to(p1.x + offR1.x, p1.y + offR1.y);
        path.line_to(p1.x + offL1.x, p1.y + offL1.y);
        path.close();
    }
}

