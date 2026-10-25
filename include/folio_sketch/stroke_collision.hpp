#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/stroke_collision.hpp
 * @brief High-Performance Computational Geometry & Slicing for Vector Ink Strokes
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & ALGORITHMS:
 * ----------------------------------
 * Implements two-tier collision detection and precision stroke slicing:
 *   1. Two-Tier Narrowphase Hit Testing:
 *      - Tier 1: Stroke-level AABB broadphase culling.
 *      - Tier 2: On-the-fly transient segment mini-AABB evaluation in CPU registers,
 *                followed by exact Euclidean point-to-segment distance calculation.
 *   2. Dan Sunday's Segment-to-Segment Minimum Distance Algorithm (Swept Capsule):
 *      - Evaluates swept stylus trajectories without tunneling at fast stroke speeds.
 *   3. Quadratic Circle-Segment Intersection Solver (StrokeSlicer):
 *      - Slices stroke segments against eraser circle kernels with sub-segment splitting.
 */

#include <vector>
#include <cmath>
#include <algorithm>

#include "folio_sketch/ink_stroke_data.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

class StrokeCollisionEngine {
public:
    struct SegmentHit {
        bool hit = false;
        size_t segmentIndex = 0;
        float t = 0.0f;
        double distSq = 0.0;
    };

    /**
     * @brief Evaluates whether a query circle intersects any segment of a stroke.
     */
    [[nodiscard]] static SegmentHit HitTestStroke(
        const std::vector<Segment1D>& segments,
        double qx, double qy, double queryRadius) noexcept
    {
        SegmentHit bestHit;
        bestHit.distSq = 1e30;

        for (size_t i = 0; i < segments.size(); ++i) {
            const auto& seg = segments[i];
            float t = 0.0f;
            double distSq = 0.0;
            if (HitTestSegment(seg.p0, seg.p1, seg.width, qx, qy, queryRadius, t, distSq)) {
                if (distSq < bestHit.distSq) {
                    bestHit.hit = true;
                    bestHit.segmentIndex = i;
                    bestHit.t = t;
                    bestHit.distSq = distSq;
                }
            }
        }

        return bestHit;
    }

    /**
     * @brief Point hit test for single-point strokes (e.g. period / tap dots).
     */
    [[nodiscard]] static inline bool HitTestPoint(
        const Point2D& p, float width,
        double qx, double qy, double queryRadius) noexcept
    {
        const double maxR = std::max(0.2, static_cast<double>(width) * 0.5 + queryRadius);
        const double dx = p.x - qx;
        const double dy = p.y - qy;
        return (dx * dx + dy * dy) <= (maxR * maxR);
    }

    /**
     * @brief Continuous swept-capsule collision test against all segments of a stroke.
     * Detects hits regardless of mouse movement speed (zero tunneling/skipping).
     */
    [[nodiscard]] static inline bool HitTestStrokeSwept(
        const std::vector<Segment1D>& segments,
        const Point2D& w0, const Point2D& w1, double queryRadius) noexcept
    {
        const double qMinX = std::min(w0.x, w1.x);
        const double qMaxX = std::max(w0.x, w1.x);
        const double qMinY = std::min(w0.y, w1.y);
        const double qMaxY = std::max(w0.y, w1.y);

        for (const auto& seg : segments) {
            const double maxR = std::max(0.0, queryRadius + static_cast<double>(seg.width) * 0.5);
            const double segMinX = std::min(seg.p0.x, seg.p1.x) - maxR;
            const double segMaxX = std::max(seg.p0.x, seg.p1.x) + maxR;
            const double segMinY = std::min(seg.p0.y, seg.p1.y) - maxR;
            const double segMaxY = std::max(seg.p0.y, seg.p1.y) + maxR;

            if (qMinX > segMaxX || qMaxX < segMinX || qMinY > segMaxY || qMaxY < segMinY) {
                continue;
            }

            const double distSq = SegmentSegmentDistanceSq(seg.p0, seg.p1, w0, w1);
            if (distSq <= maxR * maxR) {
                return true;
            }
        }
        return false;
    }

private:
    /**
     * @brief Computes point-to-line-segment distance with transient register-only mini-AABB rejection.
     */
    [[nodiscard]] static inline bool HitTestSegment(
        const Point2D& p0, const Point2D& p1, float width,
        double qx, double qy, double queryRadius,
        float& outT, double& outDistSq) noexcept
    {
        const double maxR = std::max(0.0, static_cast<double>(width) * 0.5 + queryRadius);

        // 1. Transient segment mini-AABB in CPU registers (zero memory allocation)
        const double segMinX = std::min(p0.x, p1.x) - maxR;
        const double segMaxX = std::max(p0.x, p1.x) + maxR;
        const double segMinY = std::min(p0.y, p1.y) - maxR;
        const double segMaxY = std::max(p0.y, p1.y) + maxR;

        // Fast-out: culls out 90%+ of segments with simple register comparisons
        if (qx < segMinX || qx > segMaxX || qy < segMinY || qy > segMaxY) {
            return false;
        }

        // 2. Exact Euclidean Point-to-Segment Math
        const double vx = p1.x - p0.x;
        const double vy = p1.y - p0.y;
        const double wx = qx - p0.x;
        const double wy = qy - p0.y;

        const double lenSq = vx * vx + vy * vy;
        double t = 0.0;

        if (lenSq > 1e-9) {
            const double dot = wx * vx + wy * vy;
            t = std::clamp(dot / lenSq, 0.0, 1.0);
        }

        const double projX = p0.x + t * vx;
        const double projY = p0.y + t * vy;

        const double dx = qx - projX;
        const double dy = qy - projY;
        const double distSq = dx * dx + dy * dy;

        if (distSq <= maxR * maxR) {
            outT = static_cast<float>(t);
            outDistSq = distSq;
            return true;
        }

        return false;
    }

    /**
     * @brief Computes minimum distance squared between two line segments in 2D space.
     * Implements Dan Sunday's robust segment-to-segment algorithm with zero heap allocation.
     */
    [[nodiscard]] static inline double SegmentSegmentDistanceSq(
        const Point2D& p0, const Point2D& p1,
        const Point2D& q0, const Point2D& q1) noexcept
    {
        const double ux = p1.x - p0.x;
        const double uy = p1.y - p0.y;
        const double vx = q1.x - q0.x;
        const double vy = q1.y - q0.y;
        const double wx = p0.x - q0.x;
        const double wy = p0.y - q0.y;

        const double a = ux * ux + uy * uy;
        const double b = ux * vx + uy * vy;
        const double c = vx * vx + vy * vy;
        const double d = ux * wx + uy * wy;
        const double e = vx * wx + vy * wy;

        // Handle degenerate segment cases (points) to eliminate division-by-zero
        if (a < 1e-9 && c < 1e-9) {
            return wx * wx + wy * wy;
        }
        if (a < 1e-9) {
            const double t = std::clamp(e / c, 0.0, 1.0);
            const double dx = wx - t * vx;
            const double dy = wy - t * vy;
            return dx * dx + dy * dy;
        }
        if (c < 1e-9) {
            const double s = std::clamp(-d / a, 0.0, 1.0);
            const double dx = wx + s * ux;
            const double dy = wy + s * uy;
            return dx * dx + dy * dy;
        }

        const double D = a * c - b * b;
        double s = 0.0;
        double t = 0.0;

        if (D < 1e-9) {
            // Parallel segments
            s = 0.0;
            t = std::clamp(e / c, 0.0, 1.0);
            if (t <= 0.0) {
                s = std::clamp(-d / a, 0.0, 1.0);
            } else if (t >= 1.0) {
                s = std::clamp((-d + b) / a, 0.0, 1.0);
            }
        } else {
            s = std::clamp((b * e - c * d) / D, 0.0, 1.0);
            t = (b * s + e) / c;
            if (t < 0.0) {
                t = 0.0;
                s = std::clamp(-d / a, 0.0, 1.0);
            } else if (t > 1.0) {
                t = 1.0;
                s = std::clamp((-d + b) / a, 0.0, 1.0);
            }
        }

        const double dpx = wx + (s * ux) - (t * vx);
        const double dpy = wy + (s * uy) - (t * vy);
        return dpx * dpx + dpy * dpy;
    }
};

/**
 * @brief High-precision vector stroke slicer for point-based eraser tools.
 */
class StrokeSlicer {
public:
    /**
     * @brief Slices a chain of segments against a circular eraser kernel.
     * @param segments Original stroke segments.
     * @param ex Eraser center X.
     * @param ey Eraser center Y.
     * @param eraserRadius Eraser radius.
     * @param outSubStrokes Destination list of surviving segment chains.
     * @return true if the stroke was modified or split.
     */
    static bool SliceSegments(
        const std::vector<Segment1D>& segments,
        double ex, double ey, double eraserRadius,
        std::vector<std::vector<Segment1D>>& outSubStrokes)
    {
        if (segments.empty()) return false;

        outSubStrokes.clear();
        bool modified = false;
        std::vector<Segment1D> currentChain;

        auto PushChainIfValid = [&]() {
            if (!currentChain.empty()) {
                outSubStrokes.push_back(std::move(currentChain));
                currentChain.clear();
            }
        };

        auto PushSegmentIfNonDegenerate = [&](const Point2D& a, const Point2D& b, float w) {
            if (std::hypot(b.x - a.x, b.y - a.y) > 1e-4) {
                currentChain.push_back(Segment1D{ a, b, w });
            }
        };

        for (const auto& seg : segments) {
            const double effR = std::max(0.0, eraserRadius + static_cast<double>(seg.width) * 0.5);
            const double effRSq = effR * effR;

            const double dx0 = seg.p0.x - ex;
            const double dy0 = seg.p0.y - ey;
            const bool p0In = (dx0 * dx0 + dy0 * dy0) < effRSq;

            const double dx1 = seg.p1.x - ex;
            const double dy1 = seg.p1.y - ey;
            const bool p1In = (dx1 * dx1 + dy1 * dy1) < effRSq;

            // Solve quadratic intersection: || (p0 + t * V) - E ||^2 = effRSq
            const double vx = seg.p1.x - seg.p0.x;
            const double vy = seg.p1.y - seg.p0.y;
            const double wx = seg.p0.x - ex;
            const double wy = seg.p0.y - ey;

            const double a = vx * vx + vy * vy;
            const double b = 2.0 * (wx * vx + wy * vy);
            const double c = (wx * wx + wy * wy) - effRSq;

            double t1 = -1.0, t2 = -1.0;
            bool hasIntersection = false;

            if (a > 1e-9) {
                const double discr = b * b - 4.0 * a * c;
                if (discr >= 0.0) {
                    const double sqrtD = std::sqrt(discr);
                    t1 = (-b - sqrtD) / (2.0 * a);
                    t2 = (-b + sqrtD) / (2.0 * a);
                    if (t1 > t2) std::swap(t1, t2);
                    hasIntersection = true;
                }
            }

            if (!p0In && !p1In) {
                // Both endpoints outside eraser
                double tIn = hasIntersection ? std::max(0.0, t1) : 0.0;
                double tOut = hasIntersection ? std::min(1.0, t2) : 0.0;
                if (hasIntersection && tOut > tIn + 1e-5) {
                    // Segment punches entirely through the eraser: splits into 2 pieces!
                    modified = true;
                    Point2D cutIn  = LerpPoint(seg.p0, seg.p1, tIn);
                    Point2D cutOut = LerpPoint(seg.p0, seg.p1, tOut);

                    // First part terminates current sub-stroke
                    PushSegmentIfNonDegenerate(seg.p0, cutIn, seg.width);
                    PushChainIfValid();

                    // Second part begins new sub-stroke
                    PushSegmentIfNonDegenerate(cutOut, seg.p1, seg.width);
                } else {
                    // Segment completely outside
                    currentChain.push_back(seg);
                }
            } else if (!p0In && p1In) {
                // Enters eraser: cut at t1 and finish current sub-stroke
                modified = true;
                double tCut = hasIntersection ? std::clamp(t1, 0.0, 1.0) : 0.5;
                Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(seg.p0, cutPt, seg.width);
                PushChainIfValid();
            } else if (p0In && !p1In) {
                // Exits eraser: cut at t2 and start new sub-stroke
                modified = true;
                double tCut = hasIntersection ? std::clamp(t2, 0.0, 1.0) : 0.5;
                Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(cutPt, seg.p1, seg.width);
            } else {
                // Both endpoints inside: entirely deleted
                modified = true;
                PushChainIfValid();
            }
        }

        PushChainIfValid();
        return modified;
    }

private:
    /**
     * @brief Linearly interpolates two centerline points.
     */
    [[nodiscard]] static inline Point2D LerpPoint(const Point2D& p0, const Point2D& p1, double t) noexcept {
        return Point2D{
            p0.x + t * (p1.x - p0.x),
            p0.y + t * (p1.y - p0.y)
        };
    }
};

} // namespace Folio

using StrokeCollisionEngine = Folio::StrokeCollisionEngine;
using StrokeSlicer = Folio::StrokeSlicer;
