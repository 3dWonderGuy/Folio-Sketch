#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/core/stroke_collision.hpp
 * @brief High-Performance Computational Geometry & Precision Slicing for Ink Strokes
 * =========================================================================================
 *
 * Provides two primary algorithmic facilities:
 *
 * 1. StrokeCollisionEngine:
 *    - Point / Tap hit-testing against stroke segments.
 *    - Two-tier static circle hit-testing (mini-AABB fast cull + exact point-to-segment distance).
 *    - Continuous swept-capsule collision testing (Dan Sunday's segment-to-segment algorithm)
 *      to guarantee zero tunneling / skipping regardless of fast pen/mouse motion.
 *
 * 2. StrokeSlicer:
 *    - Analytic quadratic circle-segment intersection solver for precision vector erasers.
 *    - Slices stroke segments into surviving sub-strokes when punched through by an eraser.
 */

#include <vector>
#include <cmath>
#include <algorithm>

#include "folio_sketch/core/ink_stroke_data.hpp"

namespace Folio {

// =============================================================================
// 1. STROKE COLLISION ENGINE
// =============================================================================

class StrokeCollisionEngine {
public:
    /**
     * @struct SegmentHit
     * @brief Detailed collision hit record containing segment index, parametric t, and distance.
     */
    struct SegmentHit {
        bool   hit          = false; // True if an intersection occurred
        size_t segmentIndex = 0;     // Index of the closest hit segment
        float  t            = 0.0f;  // Parametric position along the segment [0.0, 1.0]
        double distSq       = 0.0;   // Squared Euclidean distance from query point to segment
    };

    /**
     * @brief Point hit test for single-point strokes (e.g. stationary tap dots, periods).
     * @param p Stroke point center coordinate.
     * @param width Stroke diameter in world millimeters (mm).
     * @param qx Query circle center X (mm).
     * @param qy Query circle center Y (mm).
     * @param queryRadius Query circle radius (mm).
     * @return True if the query circle touches or encloses the point dot.
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
     * @brief Evaluates whether a static query circle intersects any segment of a stroke.
     * Uses two-tier testing: transient register mini-AABB culling followed by exact distance.
     *
     * @param segments Vector of stroke segments.
     * @param qx Query circle center X (mm).
     * @param qy Query circle center Y (mm).
     * @param queryRadius Query circle radius (mm).
     * @return SegmentHit record containing the closest segment hit data.
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
     * @brief Continuous swept-capsule collision test against all segments of a stroke.
     * Guarantees zero tunneling/skipping even during high-velocity pen sweeps.
     *
     * @param segments Stroke segments.
     * @param w0 Query swept segment start coordinate (mm).
     * @param w1 Query swept segment end coordinate (mm).
     * @param queryRadius Query swept radius (mm).
     * @return True if the swept capsule intersects any segment of the stroke.
     */
    [[nodiscard]] static inline bool HitTestStrokeSwept(
        const std::vector<Segment1D>& segments,
        const Point2D& w0, const Point2D& w1, double queryRadius) noexcept
    {
        // Compute bounding box for the query sweep trajectory
        const double qMinX = std::min(w0.x, w1.x);
        const double qMaxX = std::max(w0.x, w1.x);
        const double qMinY = std::min(w0.y, w1.y);
        const double qMaxY = std::max(w0.y, w1.y);

        for (const auto& seg : segments) {
            const double maxR = std::max(0.0, queryRadius + static_cast<double>(seg.width) * 0.5);

            // Fast-cull broadphase AABB overlap test
            const double segMinX = std::min(seg.p0.x, seg.p1.x) - maxR;
            const double segMaxX = std::max(seg.p0.x, seg.p1.x) + maxR;
            const double segMinY = std::min(seg.p0.y, seg.p1.y) - maxR;
            const double segMaxY = std::max(seg.p0.y, seg.p1.y) + maxR;

            if (qMinX > segMaxX || qMaxX < segMinX || qMinY > segMaxY || qMaxY < segMinY) {
                continue;
            }

            // Narrowphase: Dan Sunday's segment-to-segment minimum distance
            const double distSq = SegmentSegmentDistanceSq(seg.p0, seg.p1, w0, w1);
            if (distSq <= maxR * maxR) {
                return true;
            }
        }
        return false;
    }

private:
    /**
     * @brief Computes point-to-line-segment distance with transient register mini-AABB rejection.
     */
    [[nodiscard]] static inline bool HitTestSegment(
        const Point2D& p0, const Point2D& p1, float width,
        double qx, double qy, double queryRadius,
        float& outT, double& outDistSq) noexcept
    {
        const double maxR = std::max(0.0, static_cast<double>(width) * 0.5 + queryRadius);

        // Step 1: Transient segment mini-AABB in CPU registers (zero heap allocations)
        const double segMinX = std::min(p0.x, p1.x) - maxR;
        const double segMaxX = std::max(p0.x, p1.x) + maxR;
        const double segMinY = std::min(p0.y, p1.y) - maxR;
        const double segMaxY = std::max(p0.y, p1.y) + maxR;

        // Fast-out: culls 90%+ of non-intersecting segments with simple scalar comparisons
        if (qx < segMinX || qx > segMaxX || qy < segMinY || qy > segMaxY) {
            return false;
        }

        // Step 2: Exact Euclidean Point-to-Segment Projection
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
     * @brief Computes minimum squared distance between two line segments in 2D space.
     * Implements Dan Sunday's segment-to-segment algorithm with zero heap allocations.
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

        // Handle degenerate point-like segments to prevent division by zero
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

// =============================================================================
// 2. VECTOR STROKE SLICER (Point-Based Eraser Engine)
// =============================================================================

/**
 * @class StrokeSlicer
 * @brief High-precision vector stroke slicer for point-based eraser tools.
 */
class StrokeSlicer {
public:
    /**
     * @brief Slices a chain of segments against a circular eraser kernel.
     * Sub-segments intersecting the eraser boundary are cleanly subdivided.
     *
     * @param segments Original stroke segments.
     * @param ex Eraser center X coordinate (mm).
     * @param ey Eraser center Y coordinate (mm).
     * @param eraserRadius Eraser radius (mm).
     * @param outSubStrokes Destination vector of surviving contiguous segment chains.
     * @return True if the stroke was modified, trimmed, or split.
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

            // Solve quadratic circle-segment intersection: || (p0 + t * V) - E ||^2 = effRSq
            const double vx = seg.p1.x - seg.p0.x;
            const double vy = seg.p1.y - seg.p0.y;
            const double wx = seg.p0.x - ex;
            const double wy = seg.p0.y - ey;

            const double a = vx * vx + vy * vy;
            const double b = 2.0 * (wx * vx + wy * vy);
            const double c = (wx * wx + wy * wy) - effRSq;

            double t1 = -1.0;
            double t2 = -1.0;
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

            // Case A: Both endpoints lie outside the eraser kernel
            if (!p0In && !p1In) {
                double tIn  = hasIntersection ? std::max(0.0, t1) : 0.0;
                double tOut = hasIntersection ? std::min(1.0, t2) : 0.0;

                if (hasIntersection && tOut > tIn + 1e-5) {
                    // Segment punches entirely through the eraser: splits into 2 pieces!
                    modified = true;
                    Point2D cutIn  = LerpPoint(seg.p0, seg.p1, tIn);
                    Point2D cutOut = LerpPoint(seg.p0, seg.p1, tOut);

                    // First piece terminates the current sub-stroke chain
                    PushSegmentIfNonDegenerate(seg.p0, cutIn, seg.width);
                    PushChainIfValid();

                    // Second piece begins the next sub-stroke chain
                    PushSegmentIfNonDegenerate(cutOut, seg.p1, seg.width);
                } else {
                    // Segment completely misses eraser: retain intact
                    currentChain.push_back(seg);
                }
            }
            // Case B: Segment enters the eraser (p0 outside, p1 inside)
            else if (!p0In && p1In) {
                modified = true;
                double tCut = hasIntersection ? std::clamp(t1, 0.0, 1.0) : 0.5;
                Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(seg.p0, cutPt, seg.width);
                PushChainIfValid();
            }
            // Case C: Segment exits the eraser (p0 inside, p1 outside)
            else if (p0In && !p1In) {
                modified = true;
                double tCut = hasIntersection ? std::clamp(t2, 0.0, 1.0) : 0.5;
                Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(cutPt, seg.p1, seg.width);
            }
            // Case D: Both endpoints inside eraser kernel (completely erased)
            else {
                modified = true;
                PushChainIfValid();
            }
        }

        PushChainIfValid();
        return modified;
    }

private:
    /**
     * @brief Linearly interpolates between two 2D points.
     */
    [[nodiscard]] static inline Point2D LerpPoint(const Point2D& p0, const Point2D& p1, double t) noexcept {
        return Point2D{
            p0.x + t * (p1.x - p0.x),
            p0.y + t * (p1.y - p0.y)
        };
    }
};

} // namespace Folio
