#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/engine/stroke_post_processor.hpp
 * @brief Post-Processing and Curve Simplification for Vector Ink
 * =========================================================================================
 *
 * Responsibilities:
 * - Curve Simplification: Decimates redundant collinear vertices via a shape-preserving
 *   Ramer-Douglas-Peucker (RDP) algorithm, retaining sharp corners and dynamic thickness.
 */

#include <vector>
#include <cmath>
#include <algorithm>
#include "core/ink_stroke_data.hpp"

namespace Folio {

class StrokePostProcessor {
public:
    /**
     * @brief Simplifies stroke centerline points by decimating redundant collinear vertices.
     *
     * Shape-preserving Ramer-Douglas-Peucker: a span [A, B] is collapsed into a single chord only if
     * every interior point satisfies ALL of:
     *  - perpendicular distance to the chord <= toleranceMm
     *  - direction deviation of its segment  <= maxTurnDeg (prevents faceted peaks on arcs)
     *  - radius deviation from linear interp <= radiusTolerance * r (preserves thickness profile)
     *
     * @param points Input centerline vertices.
     * @param toleranceMm Distance tolerance in millimeters.
     * @param maxTurnDeg Maximum angular deviation (degrees) between any dropped segment and the chord.
     * @param radiusTolerance Maximum relative radius error for dropped points.
     * @return Decimated vertex vector.
     */
    [[nodiscard]] static std::vector<StrokePoint> Simplify(
        const std::vector<StrokePoint>& points,
        float toleranceMm = 0.015f,
        float maxTurnDeg = 4.0f,
        float radiusTolerance = 0.08f) {
        if (points.size() <= 2 || toleranceMm <= 0.0f) {
            return points;
        }

        const size_t n = points.size();
        std::vector<bool> keep(n, false);
        keep[0] = true;
        keep[n - 1] = true;

        std::vector<std::pair<size_t, size_t>> stack;
        stack.reserve(64);
        stack.push_back({ 0, n - 1 });

        const double tol = static_cast<double>(toleranceMm);
        const double cosMaxTurn = std::cos(static_cast<double>(maxTurnDeg) * 3.14159265358979323846 / 180.0);

        while (!stack.empty()) {
            auto [startIdx, endIdx] = stack.back();
            stack.pop_back();

            if (endIdx <= startIdx + 1) continue;

            const StrokePoint& A = points[startIdx];
            const StrokePoint& B = points[endIdx];
            const double abx = B.x - A.x;
            const double aby = B.y - A.y;
            const double abLenSq = abx * abx + aby * aby;
            const double abLen = std::sqrt(abLenSq);

            double maxErr = 0.0;   // normalized error (> 1.0 means constraint violated)
            size_t maxIdx = startIdx;

            for (size_t i = startIdx + 1; i < endIdx; ++i) {
                const StrokePoint& P = points[i];

                // 1. Perpendicular distance
                double dist = 0.0;
                double t = 0.0;
                if (abLenSq < 1e-12) {
                    dist = std::hypot(P.x - A.x, P.y - A.y);
                } else {
                    dist = std::abs((P.x - A.x) * aby - (P.y - A.y) * abx) / abLen;
                    t = std::clamp(((P.x - A.x) * abx + (P.y - A.y) * aby) / abLenSq, 0.0, 1.0);
                }
                double err = dist / tol;

                // 2. Radius profile deviation
                double rInterp = A.r + (B.r - A.r) * t;
                double rTol = std::max(1e-4, static_cast<double>(radiusTolerance) * std::max<double>(P.r, rInterp));
                err = std::max(err, std::abs(P.r - rInterp) / rTol);

                // 3. Direction deviation of the incoming segment (i-1 -> i) relative to chord
                if (abLen > 1e-9) {
                    const StrokePoint& Q = points[i - 1];
                    double sx = P.x - Q.x, sy = P.y - Q.y;
                    double sLen = std::hypot(sx, sy);
                    if (sLen > 1e-9) {
                        double cosA = (sx * abx + sy * aby) / (sLen * abLen);
                        if (cosA < cosMaxTurn) {
                            err = std::max(err, 1.0 + (cosMaxTurn - cosA));
                        }
                    }
                }

                if (err > maxErr) {
                    maxErr = err;
                    maxIdx = i;
                }
            }

            if (maxErr > 1.0) {
                keep[maxIdx] = true;
                stack.push_back({ startIdx, maxIdx });
                stack.push_back({ maxIdx, endIdx });
            }
        }

        std::vector<StrokePoint> result;
        result.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (keep[i]) {
                result.push_back(points[i]);
            }
        }
        return result;
    }

    /**
     * @brief Smooths the radius profile of a stroke along its arc-length using a spatial Gaussian filter.
     *
     * Converts sharp, jagged thickness peaks and sawtooth spikes into soft, rounded hills.
     * Operates in true physical millimeter arc-length space so smoothing is invariant to point density.
     *
     * @param points Input/output stroke points whose .r radii are smoothed in-place.
     * @param sigmaMm Standard deviation of the Gaussian kernel in millimeters (e.g. 2.0 - 5.0 mm).
     * @param passes Number of smoothing passes (e.g. 1 or 2).
     */
    static void SmoothRadiiGaussian(std::vector<StrokePoint>& points,
                                    float sigmaMm = 3.5f,
                                    int passes = 2) noexcept {
        const size_t n = points.size();
        if (n < 3 || sigmaMm <= 0.01f || passes <= 0) return;

        // Compute cumulative arc-length distances
        std::vector<float> arcLengths(n, 0.0f);
        for (size_t i = 1; i < n; ++i) {
            float ds = static_cast<float>(std::hypot(points[i].x - points[i - 1].x,
                                                     points[i].y - points[i - 1].y));
            arcLengths[i] = arcLengths[i - 1] + ds;
        }

        const float twoSigmaSq = 2.0f * sigmaMm * sigmaMm;
        const float maxWindowDist = 3.0f * sigmaMm; // 3-sigma cutoff (~99.7% energy)

        std::vector<float> originalRadii(n);
        std::vector<float> smoothedRadii(n);

        for (int p = 0; p < passes; ++p) {
            for (size_t i = 0; i < n; ++i) {
                originalRadii[i] = points[i].r;
            }

            for (size_t i = 0; i < n; ++i) {
                float centerS = arcLengths[i];
                float weightSum = 0.0f;
                float radiusSum = 0.0f;

                // Expand backwards along arc-length within 3-sigma window
                for (int j = static_cast<int>(i); j >= 0; --j) {
                    float dist = centerS - arcLengths[j];
                    if (dist > maxWindowDist) break;
                    float w = std::exp(-(dist * dist) / twoSigmaSq);
                    weightSum += w;
                    radiusSum += originalRadii[j] * w;
                }

                // Expand forwards along arc-length within 3-sigma window
                for (size_t j = i + 1; j < n; ++j) {
                    float dist = arcLengths[j] - centerS;
                    if (dist > maxWindowDist) break;
                    float w = std::exp(-(dist * dist) / twoSigmaSq);
                    weightSum += w;
                    radiusSum += originalRadii[j] * w;
                }

                smoothedRadii[i] = (weightSum > 1e-5f) ? (radiusSum / weightSum) : originalRadii[i];
            }

            for (size_t i = 0; i < n; ++i) {
                points[i].r = smoothedRadii[i];
            }
        }
    }
};

} // namespace Folio
