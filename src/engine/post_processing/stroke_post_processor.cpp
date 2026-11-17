/**
 * =========================================================================================
 * @file src/engine/post_processing/stroke_post_processor.cpp
 * @brief Post-Processing and Curve Simplification for Vector Ink
 * =========================================================================================
 *
 * Two filters run on a finished stroke:
 *  1. Simplify:            removes redundant points (shape-preserving Ramer-Douglas-Peucker).
 *  2. SmoothRadiiGaussian: softens jagged thickness changes along the stroke.
 */

#include "engine/post_processing/stroke_post_processor.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Folio {

// =============================================================================
// Simplify: shape-preserving Ramer-Douglas-Peucker (RDP)
// =============================================================================
//
// Classic RDP idea:
//   1. Draw a straight chord from the first point (A) to the last point (B).
//   2. Find the interior point that deviates the MOST from that chord.
//   3. If the deviation is small enough, drop all interior points (the chord is good enough).
//   4. Otherwise keep that worst point and repeat steps 1-3 on the two halves [A..worst], [worst..B].
//
// Unlike classic RDP (which only measures distance), "deviation" here combines three checks,
// each normalized so that a value > 1.0 means "this constraint is violated":
//   - distance from chord / toleranceMm        (position)
//   - radius error / allowed radius error      (stroke thickness)
//   - segment direction vs chord direction     (prevents flat-faceted arcs)
// The largest of the three is used as the point's error.
std::vector<StrokePoint> StrokePostProcessor::Simplify(
    const std::vector<StrokePoint>& points,
    float toleranceMm,
    float maxTurnDeg,
    float radiusTolerance)
{
    // Nothing to simplify: 0-2 points can't have redundant interior points,
    // and a non-positive tolerance means "keep everything".
    if (points.size() <= 2 || toleranceMm <= 0.0f) {
        return points;
    }

    const size_t n = points.size();

    // keep[i] == true means point i survives. First and last points are always kept.
    std::vector<bool> keep(n, false);
    keep[0] = true;
    keep[n - 1] = true;

    // Work list of index ranges [start, end] still to be examined.
    // An explicit stack replaces recursion so very long strokes can't overflow the call stack.
    std::vector<std::pair<size_t, size_t>> stack;
    stack.reserve(64);
    stack.push_back({ 0, n - 1 });

    const double tol = static_cast<double>(toleranceMm);

    // Compare angles through cosine (cheaper than acos per point).
    // A smaller cosine means a larger angle, so "cosA < cosMaxTurn" means "turned too much".
    const double cosMaxTurn = std::cos(static_cast<double>(maxTurnDeg) * 3.14159265358979323846 / 180.0);

    // Process ranges until the stack is empty
    while (!stack.empty()) {
        auto [startIdx, endIdx] = stack.back();
        stack.pop_back();

        // Range has no interior points; nothing to decide.
        if (endIdx <= startIdx + 1) continue;

        // Chord from A (range start) to B (range end)
        const StrokePoint& A = points[startIdx];
        const StrokePoint& B = points[endIdx];
        const double abx = B.x - A.x;
        const double aby = B.y - A.y;
        const double abLenSq = abx * abx + aby * aby;
        const double abLen = std::sqrt(abLenSq);

        double maxErr = 0.0;        // worst normalized error in this range (> 1.0 = violation)
        size_t maxIdx = startIdx;   // index of the point that produced it

        // Loop through all interior points and find the one that deviates the most from the chord.
        for (size_t i = startIdx + 1; i < endIdx; ++i) {
            const StrokePoint& P = points[i];

            // --- Check 1: perpendicular distance from P to the chord A-B ---
            // 't' is where P projects onto the chord (0 = at A, 1 = at B); reused for the radius check.
            double dist = 0.0;
            double t = 0.0;
            if (abLenSq < 1e-12) {
                // A and B coincide (closed loop): chord has no direction, use plain distance to A.
                dist = std::hypot(P.x - A.x, P.y - A.y);
            } else {
                // |cross product| / chord length = perpendicular distance
                dist = std::abs((P.x - A.x) * aby - (P.y - A.y) * abx) / abLen;
                // dot product / chord length^2 = projection position, clamped to the chord
                t = std::clamp(((P.x - A.x) * abx + (P.y - A.y) * aby) / abLenSq, 0.0, 1.0);
            }
            double err = dist / tol;

            // --- Check 2: stroke thickness (radius) deviation ---
            // If we dropped P, the radius there would be linearly interpolated between A and B.
            // Compare that with the real radius. Tolerance scales with the radius so thin and
            // thick parts of the stroke are treated proportionally.
            double rInterp = A.r + (B.r - A.r) * t;
            double rTol = std::max(1e-4, static_cast<double>(radiusTolerance) * std::max<double>(P.r, rInterp));
            err = std::max(err, std::abs(P.r - rInterp) / rTol);

            // --- Check 3: direction deviation ---
            // Angle between the segment arriving at P (Q -> P) and the chord direction.
            // Without this, a smooth arc could be replaced by a few long chords that are each
            // within distance tolerance but visibly faceted.
            if (abLen > 1e-9) {
                const StrokePoint& Q = points[i - 1];
                double sx = P.x - Q.x, sy = P.y - Q.y;
                double sLen = std::hypot(sx, sy);
                if (sLen > 1e-9) {
                    // cos(angle) = dot(segment, chord) / (|segment| * |chord|)
                    double cosA = (sx * abx + sy * aby) / (sLen * abLen);
                    if (cosA < cosMaxTurn) {
                        // Turned more than allowed: force error above 1.0 (bigger turn = bigger error)
                        err = std::max(err, 1.0 + (cosMaxTurn - cosA));
                    }
                }
            }

            // Remember the worst offender in this range
            if (err > maxErr) {
                maxErr = err;
                maxIdx = i;
            }
        }

        // If even the worst point violates a constraint, the chord isn't good enough:
        // keep that point and split the range there. Otherwise all interior points
        // stay un-kept (dropped).
        if (maxErr > 1.0) {
            keep[maxIdx] = true;
            stack.push_back({ startIdx, maxIdx });
            stack.push_back({ maxIdx, endIdx });
        }
    }

    // Build the output from only the surviving points, preserving original order
    std::vector<StrokePoint> result;
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (keep[i]) {
            result.push_back(points[i]);
        }
    }
    return result;
}

// =============================================================================
// SmoothRadiiGaussian: spatial Gaussian blur of stroke thickness
// =============================================================================
//
// Each point's new radius is a weighted average of nearby radii, where weight falls off as a
// Gaussian of the physical distance (in mm) ALONG the stroke:
//     w(d) = exp(-d^2 / (2 * sigma^2))
// Using millimeters instead of point index makes the result independent of how densely
// the points were sampled (fast vs slow pen movement).
void StrokePostProcessor::SmoothRadiiGaussian(
    std::vector<StrokePoint>& points,
    float sigmaMm,
    int passes) noexcept
{
    const size_t n = points.size();

    // Too few points, no meaningful sigma, or no passes requested: leave the stroke untouched.
    if (n < 3 || sigmaMm <= 0.01f || passes <= 0) return;

    // Per-thread scratch buffers. They keep their capacity between calls, so after the first
    // stroke of a given size there are no further heap allocations.
    thread_local std::vector<float> arcLengths;   // distance from stroke start to each point (mm)
    thread_local std::vector<float> radiiA;       // radius buffer 1
    thread_local std::vector<float> radiiB;       // radius buffer 2

    arcLengths.assign(n, 0.0f);
    radiiA.resize(n);
    radiiB.resize(n);

    // Cumulative arc length: arcLengths[i] = total path length from point 0 to point i
    for (size_t i = 1; i < n; ++i) {
        float ds = static_cast<float>(std::hypot(points[i].x - points[i - 1].x,
                                                 points[i].y - points[i - 1].y));
        arcLengths[i] = arcLengths[i - 1] + ds;
    }

    const float twoSigmaSq = 2.0f * sigmaMm * sigmaMm;

    // Beyond 3 sigma the Gaussian weight is negligible (~99.7% of the weight lies within),
    // so we stop searching there. This keeps the cost near O(N) instead of O(N^2).
    const float maxWindowDist = 3.0f * sigmaMm;

    // Seed the first buffer with the current radii
    for (size_t i = 0; i < n; ++i) {
        radiiA[i] = points[i].r;
    }

    // Ping-pong between two buffers: each pass reads from 'src' and writes to 'dst',
    // then they swap. This avoids copying the data back after every pass.
    std::vector<float>* src = &radiiA;
    std::vector<float>* dst = &radiiB;

    for (int p = 0; p < passes; ++p) {
        const std::vector<float>& in = *src;
        std::vector<float>& out = *dst;

        for (size_t i = 0; i < n; ++i) {
            const float centerS = arcLengths[i];   // position of the point being smoothed
            float weightSum = 0.0f;                // sum of weights (for normalization)
            float radiusSum = 0.0f;                // sum of weight * radius

            // Walk backwards (including the point itself, dist = 0, weight = 1)
            // until we leave the 3-sigma window.
            for (int j = static_cast<int>(i); j >= 0; --j) {
                float dist = centerS - arcLengths[j];
                if (dist > maxWindowDist) break;
                float w = std::exp(-(dist * dist) / twoSigmaSq);
                weightSum += w;
                radiusSum += in[j] * w;
            }

            // Walk forwards (excluding the point itself, already counted above)
            // until we leave the 3-sigma window.
            for (size_t j = i + 1; j < n; ++j) {
                float dist = arcLengths[j] - centerS;
                if (dist > maxWindowDist) break;
                float w = std::exp(-(dist * dist) / twoSigmaSq);
                weightSum += w;
                radiusSum += in[j] * w;
            }

            // Weighted average. The fallback guards against division by ~0 (shouldn't happen
            // because the point's own weight is 1, but keeps the math safe).
            out[i] = (weightSum > 1e-5f) ? (radiusSum / weightSum) : in[i];
        }

        // Output of this pass becomes the input of the next
        std::swap(src, dst);
    }

    // After the final swap, 'src' points at the most recent result; write it back to the stroke
    for (size_t i = 0; i < n; ++i) {
        points[i].r = (*src)[i];
    }
}

} // namespace Folio
