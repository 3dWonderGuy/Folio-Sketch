#include "folio_sketch/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

namespace Folio {

// =============================================================================
// DISCONTINUOUS PATTERN DISPATCHER
// =============================================================================

/**
 * @brief Dispatches non-solid stroke pattern styles.
 *
 * Computes specialized stroke geometries:
 * - Dotted: Sequence of discrete circular dots with pitch proportional to local stroke thickness.
 * - Dashed: Arc-length parameterized dash-and-gap sequences with round-capped sub-ribbons.
 * - DashDot: Alternating dash segments and discrete circular dots.
 * - TexturedPencil: Physically-modeled graphite lead contact patch accounting for stylus tilt.
 *
 * @param pts Centerline vertices with radii and stylus telemetry.
 * @param pattern Selected stroke pattern.
 * @return Closed InkPath geometry.
 */
InkPath InkGenerator::ApplyDiscontinuousPattern(const std::vector<StrokePoint>& pts,
                                               StrokePattern pattern) {
    switch (pattern) {
        case StrokePattern::Dotted:
            return BuildDottedPattern(pts);
        case StrokePattern::Dashed:
            return BuildDashedPattern(pts);
        case StrokePattern::DashDot:
            return BuildDashDotPattern(pts);
        case StrokePattern::TexturedPencil:
            return BuildTexturedPencilPattern(pts);
        case StrokePattern::Solid:
        default:
            return BuildSolidRibbon(pts);
    }
}

// =============================================================================
// 1. DOTTED PATTERN
// =============================================================================

/**
 * @brief Generates an arc-length parameterized sequence of circular dots along the centerline.
 *
 * Rather than placing dots at raw vertex locations (which would create uneven clustering during
 * slow stylus movement), this algorithm parameterizes the centerline by arc length and emits
 * dots with spacing dynamically proportional to stroke radius:
 *   spacing = max(0.6 mm, dotR * 4.4)
 */
InkPath InkGenerator::BuildDottedPattern(const std::vector<StrokePoint>& pts) {
    InkPath path;
    if (pts.empty()) return path;
    if (pts.size() == 1) {
        path.addCircle(pts[0].x, pts[0].y, pts[0].r);
        return path;
    }

    double nextDotDist = 0.0;
    double distAccum = 0.0;

    for (size_t i = 0; i < pts.size() - 1; ++i) {
        const auto& p0 = pts[i];
        const auto& p1 = pts[i + 1];
        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double segLen = std::hypot(dx, dy);
        if (segLen < 1e-7) continue;

        // Step along segment by nextDotDist intervals
        while (nextDotDist <= distAccum + segLen) {
            double t = (nextDotDist - distAccum) / segLen;
            t = std::clamp(t, 0.0, 1.0);
            double dotX = p0.x + t * dx;
            double dotY = p0.y + t * dy;
            float dotR = p0.r + static_cast<float>(t) * (p1.r - p0.r);
            double r = dotR;
            path.addCircle(dotX, dotY, r);

            // Dynamic spacing scales with dot size so thick lines have appropriately spaced dots
            double spacing = std::max(0.6, (double)dotR * 4.4);
            nextDotDist += spacing;
        }
        distAccum += segLen;
    }
    if (path.is_empty() && !pts.empty()) {
        double r = pts[0].r;
        path.addCircle(pts[0].x, pts[0].y, r);
    }
    return path;
}

// =============================================================================
// 2. DASHED PATTERN
// =============================================================================

/**
 * @brief Generates an arc-length parameterized sequence of dashed stroke segments.
 *
 * Pre-computes cumulative polyline arc length, determines dash length and gap length
 * proportional to average stroke radius, and extracts sub-ribbons using binary search sampling
 * (std::upper_bound) to guarantee smooth, evenly spaced dashes independent of input sampling rate.
 */
InkPath InkGenerator::BuildDashedPattern(const std::vector<StrokePoint>& pts) {
    InkPath path;
    if (pts.empty()) return path;
    if (pts.size() == 1) {
        path.addCircle(pts[0].x, pts[0].y, pts[0].r);
        return path;
    }

    const size_t N = pts.size();
    // Precompute cumulative arc length table
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        double r = pts[0].r;
        path.addCircle(pts[0].x, pts[0].y, r);
        return path;
    }

    // Determine average stroke radius to scale dash and gap intervals
    float avgR = 0.0f;
    for (const auto& pt : pts) avgR += pt.r;
    avgR /= static_cast<float>(N);

    double dashLen = std::max(1.8, (double)avgR * 8.0);
    double gapLen  = std::max(1.2, (double)avgR * 4.5);
    double cycleLen = dashLen + gapLen;

    // Linear interpolator across cumulative arc length
    auto sampleAt = [&](double targetDist) -> StrokePoint {
        targetDist = std::clamp(targetDist, 0.0, totalLen);
        auto it = std::upper_bound(cumDist.begin(), cumDist.end(), targetDist);
        size_t idx = (it == cumDist.begin()) ? 0 : std::distance(cumDist.begin(), it) - 1;
        if (idx >= N - 1) return pts.back();

        double segStart = cumDist[idx];
        double segEnd   = cumDist[idx + 1];
        double segSpan  = segEnd - segStart;
        double t = (segSpan > 1e-7) ? (targetDist - segStart) / segSpan : 0.0;
        t = std::clamp(t, 0.0, 1.0);

        return StrokePoint(
            pts[idx].x + t * (pts[idx + 1].x - pts[idx].x),
            pts[idx].y + t * (pts[idx + 1].y - pts[idx].y),
            pts[idx].r + static_cast<float>(t) * (pts[idx + 1].r - pts[idx].r),
            pts[idx].pressure + static_cast<float>(t) * (pts[idx + 1].pressure - pts[idx].pressure)
        );
    };

    double curDist = 0.0;
    while (curDist < totalLen) {
        double dashStart = curDist;
        double dashEnd   = std::min(totalLen, curDist + dashLen);

        std::vector<StrokePoint> dashPts;
        dashPts.push_back(sampleAt(dashStart));

        // Gather all interior vertices between dashStart and dashEnd to preserve curve fidelity
        auto it = std::upper_bound(cumDist.begin(), cumDist.end(), dashStart);
        while (it != cumDist.end() && *it < dashEnd) {
            dashPts.push_back(pts[std::distance(cumDist.begin(), it)]);
            ++it;
        }

        dashPts.push_back(sampleAt(dashEnd));

        if (dashPts.size() == 1) {
            double r = dashPts[0].r;
            path.addCircle(dashPts[0].x, dashPts[0].y, r);
        } else {
            InkPath subRibbon = BuildSolidRibbon(dashPts);
            path.add_path(subRibbon);
        }

        curDist += cycleLen;
    }
    return path;
}

// =============================================================================
// 3. DASH-DOT PATTERN
// =============================================================================

/**
 * @brief Generates an arc-length parameterized dash-dot sequence contour (long dash, gap, dot, gap).
 */
InkPath InkGenerator::BuildDashDotPattern(const std::vector<StrokePoint>& pts) {
    InkPath path;
    if (pts.empty()) return path;
    if (pts.size() == 1) {
        path.addCircle(pts[0].x, pts[0].y, pts[0].r);
        return path;
    }

    const size_t N = pts.size();
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        double r = pts[0].r;
        path.addCircle(pts[0].x, pts[0].y, r);
        return path;
    }

    float avgR = 0.0f;
    for (const auto& pt : pts) avgR += pt.r;
    avgR /= static_cast<float>(N);

    double dashLen = std::max(2.0, (double)avgR * 9.0);
    double gap1    = std::max(1.0, (double)avgR * 3.5);
    double dotGap  = std::max(1.0, (double)avgR * 3.5);
    double cycleLen = dashLen + gap1 + dotGap;

    auto sampleAt = [&](double targetDist) -> StrokePoint {
        targetDist = std::clamp(targetDist, 0.0, totalLen);
        auto it = std::upper_bound(cumDist.begin(), cumDist.end(), targetDist);
        size_t idx = (it == cumDist.begin()) ? 0 : std::distance(cumDist.begin(), it) - 1;
        if (idx >= N - 1) return pts.back();

        double segStart = cumDist[idx];
        double segEnd   = cumDist[idx + 1];
        double segSpan  = segEnd - segStart;
        double t = (segSpan > 1e-7) ? (targetDist - segStart) / segSpan : 0.0;
        t = std::clamp(t, 0.0, 1.0);

        return StrokePoint(
            pts[idx].x + t * (pts[idx + 1].x - pts[idx].x),
            pts[idx].y + t * (pts[idx + 1].y - pts[idx].y),
            pts[idx].r + static_cast<float>(t) * (pts[idx + 1].r - pts[idx].r),
            pts[idx].pressure + static_cast<float>(t) * (pts[idx + 1].pressure - pts[idx].pressure)
        );
    };

    double curDist = 0.0;
    while (curDist < totalLen) {
        // 1. Dash component
        double dashStart = curDist;
        double dashEnd   = std::min(totalLen, curDist + dashLen);

        std::vector<StrokePoint> dashPts;
        dashPts.push_back(sampleAt(dashStart));

        auto it = std::upper_bound(cumDist.begin(), cumDist.end(), dashStart);
        while (it != cumDist.end() && *it < dashEnd) {
            dashPts.push_back(pts[std::distance(cumDist.begin(), it)]);
            ++it;
        }

        dashPts.push_back(sampleAt(dashEnd));

        if (dashPts.size() == 1) {
            double r = dashPts[0].r;
            path.addCircle(dashPts[0].x, dashPts[0].y, r);
        } else {
            InkPath subRibbon = BuildSolidRibbon(dashPts);
            path.add_path(subRibbon);
        }

        // 2. Dot component
        double dotDist = curDist + dashLen + gap1;
        if (dotDist <= totalLen) {
            StrokePoint dotPt = sampleAt(dotDist);
            double r = dotPt.r;
            path.addCircle(dotPt.x, dotPt.y, r);
        }

        curDist += cycleLen;
    }
    return path;
}

// =============================================================================
// 4. TEXTURED PENCIL
// =============================================================================

/**
 * @brief Generates graphite pencil stroke boundary taking stylus tilt into account.
 *
 * Projects the contact envelope in the tilt direction and constructs the solid outer ribbon.
 */
InkPath InkGenerator::BuildTexturedPencilPattern(const std::vector<StrokePoint>& pts) {
    if (pts.empty()) return InkPath{};

    // Check if stylus tilt telemetry is present
    bool hasTilt = false;
    for (const auto& pt : pts) {
        if (pt.tiltX != 0.0f || pt.tiltY != 0.0f) {
            hasTilt = true;
            break;
        }
    }
    if (!hasTilt) {
        return BuildSolidRibbon(pts, CapType::Round);
    }

    std::vector<StrokePoint> outerPts(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& pt = pts[i];
        if (pt.tiltX == 0.0f && pt.tiltY == 0.0f) {
            outerPts[i] = pt;
            continue;
        }
        float tiltMag = std::hypot(pt.tiltX, pt.tiltY);
        double ux = (tiltMag > 1e-4f) ? (pt.tiltX / tiltMag) : 0.0;
        double uy = (tiltMag > 1e-4f) ? (pt.tiltY / tiltMag) : 0.0;

        // Smooth transition from tip contact to body contact above 0.22 rad
        float tiltFactor = 1.0f;
        if (tiltMag > 0.22f) {
            float t = std::clamp((tiltMag - 0.22f) / (1.05f - 0.22f), 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);
            tiltFactor = 1.0f + smoothT * 1.8f;
        }
        float rTip = pt.r / tiltFactor;
        float rTiltExtra = std::max(0.0f, pt.r - rTip);

        // Displace point along tilt unit vector
        outerPts[i] = StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, pt.r, pt.pressure, pt.tiltX, pt.tiltY);
    }

    return BuildSolidRibbon(outerPts, CapType::Round);
}

} // namespace Folio

