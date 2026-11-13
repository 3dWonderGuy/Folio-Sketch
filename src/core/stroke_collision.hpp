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

#include "core/ink_stroke_data.hpp"

namespace Folio {

namespace detail {

/**
 * @struct Vec2
 * @brief Lightweight 2D vector helper for geometric arithmetic with zero overhead.
 */
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(double x_, double y_) noexcept : x(x_), y(y_) {}
    constexpr Vec2(const Point2D& p) noexcept : x(static_cast<double>(p.x)), y(static_cast<double>(p.y)) {}

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& o) const noexcept { return { x + o.x, y + o.y }; }
    [[nodiscard]] constexpr Vec2 operator-(const Vec2& o) const noexcept { return { x - o.x, y - o.y }; }
    [[nodiscard]] constexpr Vec2 operator*(double s) const noexcept { return { x * s, y * s }; }

    [[nodiscard]] constexpr double Dot(const Vec2& o) const noexcept { return x * o.x + y * o.y; }
    [[nodiscard]] constexpr double LengthSq() const noexcept { return x * x + y * y; }
};

} // namespace detail

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
        const double distSq = (detail::Vec2(p) - detail::Vec2(qx, qy)).LengthSq();
        return distSq <= (maxR * maxR);
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
        // Query sweep trajectory AABB
        const double qMinX = std::min(w0.x, w1.x);
        const double qMaxX = std::max(w0.x, w1.x);
        const double qMinY = std::min(w0.y, w1.y);
        const double qMaxY = std::max(w0.y, w1.y);

        for (const auto& seg : segments) {
            const double maxR = std::max(0.0, queryRadius + static_cast<double>(seg.width) * 0.5);

            // Broadphase: fast-cull AABB overlap test
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

        // Tier 1: Transient segment mini-AABB rejection in CPU registers (zero heap allocations)
        const double segMinX = std::min(p0.x, p1.x) - maxR;
        const double segMaxX = std::max(p0.x, p1.x) + maxR;
        const double segMinY = std::min(p0.y, p1.y) - maxR;
        const double segMaxY = std::max(p0.y, p1.y) + maxR;

        if (qx < segMinX || qx > segMaxX || qy < segMinY || qy > segMaxY) {
            return false;
        }

        // Tier 2: Exact Euclidean Point-to-Segment Projection
        const detail::Vec2 v = detail::Vec2(p1) - detail::Vec2(p0);
        const detail::Vec2 w = detail::Vec2(qx, qy) - detail::Vec2(p0);

        const double lenSq = v.LengthSq();
        double t = 0.0;

        if (lenSq > 1e-9) {
            t = std::clamp(w.Dot(v) / lenSq, 0.0, 1.0);
        }

        const detail::Vec2 projection = detail::Vec2(p0) + v * t;
        const double distSq = (detail::Vec2(qx, qy) - projection).LengthSq();

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
     *
     * Finds s, t in [0, 1] minimizing || (P0 + s*u) - (Q0 + t*v) ||^2.
     */
    [[nodiscard]] static inline double SegmentSegmentDistanceSq(
        const Point2D& p0, const Point2D& p1,
        const Point2D& q0, const Point2D& q1) noexcept
    {
        const detail::Vec2 u = detail::Vec2(p1) - detail::Vec2(p0); // Direction vector of segment P
        const detail::Vec2 v = detail::Vec2(q1) - detail::Vec2(q0); // Direction vector of segment Q
        const detail::Vec2 w = detail::Vec2(p0) - detail::Vec2(q0); // Vector between segment start points

        const double uLenSq = u.Dot(u); // |u|^2 (Dan Sunday: a)
        const double uvDot  = u.Dot(v); //  u.v  (Dan Sunday: b)
        const double vLenSq = v.Dot(v); // |v|^2 (Dan Sunday: c)
        const double uwDot  = u.Dot(w); //  u.w  (Dan Sunday: d)
        const double vwDot  = v.Dot(w); //  v.w  (Dan Sunday: e)

        constexpr double kEpsilon = 1e-9;

        // Degenerate cases: one or both segments are point-like
        if (uLenSq < kEpsilon && vLenSq < kEpsilon) {
            return w.LengthSq();
        }
        if (uLenSq < kEpsilon) {
            // Segment P is a point: project P0 onto segment Q
            const double t = std::clamp(vwDot / vLenSq, 0.0, 1.0);
            return (w - v * t).LengthSq();
        }
        if (vLenSq < kEpsilon) {
            // Segment Q is a point: project Q0 onto segment P
            const double s = std::clamp(-uwDot / uLenSq, 0.0, 1.0);
            return (w + u * s).LengthSq();
        }

        // General case: compute closest parametric points (s, t) in [0, 1]
        const double denom = uLenSq * vLenSq - uvDot * uvDot; // Determinant (Dan Sunday: D)
        double s = 0.0;
        double t = 0.0;

        if (denom < kEpsilon) {
            // Parallel or nearly parallel segments: fix s = 0 and solve for t
            s = 0.0;
            t = std::clamp(vwDot / vLenSq, 0.0, 1.0);
            if (t <= 0.0) {
                s = std::clamp(-uwDot / uLenSq, 0.0, 1.0);
            } else if (t >= 1.0) {
                s = std::clamp((-uwDot + uvDot) / uLenSq, 0.0, 1.0);
            }
        } else {
            // Non-parallel lines: solve 2x2 system, then clamp to [0, 1] x [0, 1]
            s = std::clamp((uvDot * vwDot - vLenSq * uwDot) / denom, 0.0, 1.0);
            t = (uvDot * s + vwDot) / vLenSq;

            if (t < 0.0) {
                t = 0.0;
                s = std::clamp(-uwDot / uLenSq, 0.0, 1.0);
            } else if (t > 1.0) {
                t = 1.0;
                s = std::clamp((-uwDot + uvDot) / uLenSq, 0.0, 1.0);
            }
        }

        // Vector connecting closest points: S1(s) - S2(t) = w + s*u - t*v
        const detail::Vec2 diff = w + (u * s) - (v * t);
        return diff.LengthSq();
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
            if ((detail::Vec2(b) - detail::Vec2(a)).LengthSq() > 1e-8) {
                currentChain.push_back(Segment1D{ a, b, w });
            }
        };

        const detail::Vec2 eraserCenter(ex, ey);

        for (const auto& seg : segments) {
            const double effR = std::max(0.0, eraserRadius + static_cast<double>(seg.width) * 0.5);
            const double effRSq = effR * effR;

            const bool p0In = (detail::Vec2(seg.p0) - eraserCenter).LengthSq() < effRSq;
            const bool p1In = (detail::Vec2(seg.p1) - eraserCenter).LengthSq() < effRSq;

            const auto [hasIntersection, t1, t2] = FindCircleIntersections(seg.p0, seg.p1, eraserCenter, effR);

            // Case A: Both endpoints lie outside the eraser kernel
            if (!p0In && !p1In) {
                const double tIn  = hasIntersection ? std::max(0.0, t1) : 0.0;
                const double tOut = hasIntersection ? std::min(1.0, t2) : 0.0;

                if (hasIntersection && tOut > tIn + 1e-5) {
                    // Segment punches entirely through eraser: splits into 2 pieces!
                    modified = true;
                    const Point2D cutIn  = LerpPoint(seg.p0, seg.p1, tIn);
                    const Point2D cutOut = LerpPoint(seg.p0, seg.p1, tOut);

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
                const double tCut = hasIntersection ? std::clamp(t1, 0.0, 1.0) : 0.5;
                const Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(seg.p0, cutPt, seg.width);
                PushChainIfValid();
            }
            // Case C: Segment exits the eraser (p0 inside, p1 outside)
            else if (p0In && !p1In) {
                modified = true;
                const double tCut = hasIntersection ? std::clamp(t2, 0.0, 1.0) : 0.5;
                const Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

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
    struct CircleIntersectionResult {
        bool   hasIntersection = false;
        double t1              = -1.0;
        double t2              = -1.0;
    };

    /**
     * @brief Solves quadratic circle-segment intersection: || (p0 + t * v) - circleCenter ||^2 = radius^2
     */
    [[nodiscard]] static CircleIntersectionResult FindCircleIntersections(
        const Point2D& p0, const Point2D& p1,
        const detail::Vec2& circleCenter, double radius) noexcept
    {
        const detail::Vec2 v = detail::Vec2(p1) - detail::Vec2(p0);
        const detail::Vec2 w = detail::Vec2(p0) - circleCenter;

        const double a = v.LengthSq();
        if (a < 1e-9) {
            return {};
        }

        const double b = 2.0 * v.Dot(w);
        const double c = w.LengthSq() - (radius * radius);
        const double discr = b * b - 4.0 * a * c;

        if (discr < 0.0) {
            return {};
        }

        const double sqrtD = std::sqrt(discr);
        double t1 = (-b - sqrtD) / (2.0 * a);
        double t2 = (-b + sqrtD) / (2.0 * a);
        if (t1 > t2) {
            std::swap(t1, t2);
        }

        return { true, t1, t2 };
    }

    /**
     * @brief Linearly interpolates between two 2D points.
     */
    [[nodiscard]] static inline Point2D LerpPoint(const Point2D& p0, const Point2D& p1, double t) noexcept {
        return Point2D{
            static_cast<decltype(p0.x)>(p0.x + t * (p1.x - p0.x)),
            static_cast<decltype(p0.y)>(p0.y + t * (p1.y - p0.y))
        };
    }
};

} // namespace Folio
