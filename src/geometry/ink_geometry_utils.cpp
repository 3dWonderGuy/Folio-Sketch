#include "ink_geometry_utils.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {
namespace InkInternal {

std::vector<SegmentInfo> ComputeSegments(std::span<const StrokePoint> cleanPts) {
    if (cleanPts.size() < 2) return {};

    const size_t N = cleanPts.size();
    std::vector<SegmentInfo> segs(N - 1);

    for (size_t i = 0; i < N - 1; ++i) {
        double dx = cleanPts[i + 1].x - cleanPts[i].x;
        double dy = cleanPts[i + 1].y - cleanPts[i].y;
        double len = std::hypot(dx, dy);
        segs[i].len = len;
        if (len > 1e-7) {
            segs[i].ux = dx / len;
            segs[i].uy = dy / len;
            segs[i].nx =  segs[i].uy;
            segs[i].ny = -segs[i].ux;
        } else {
            if (i > 0) {
                segs[i] = segs[i - 1];
            } else {
                segs[i].ux = 1.0;
                segs[i].uy = 0.0;
                segs[i].nx = 0.0;
                segs[i].ny = -1.0;
            }
        }
    }
    return segs;
}

bool IntersectSegments(const Point2D& p1, const Point2D& p2,
                       const Point2D& p3, const Point2D& p4,
                       Point2D& outHit,
                       bool inclusive) {
    // 1. Fast AABB bounding box rejection
    double min1X = std::min(p1.x, p2.x), max1X = std::max(p1.x, p2.x);
    double min1Y = std::min(p1.y, p2.y), max1Y = std::max(p1.y, p2.y);
    double min2X = std::min(p3.x, p4.x), max2X = std::max(p3.x, p4.x);
    double min2Y = std::min(p3.y, p4.y), max2Y = std::max(p3.y, p4.y);

    if (max1X < min2X - 1e-6 || min1X > max2X + 1e-6 ||
        max1Y < min2Y - 1e-6 || min1Y > max2Y + 1e-6) {
        return false;
    }

    double d1x = p2.x - p1.x;
    double d1y = p2.y - p1.y;
    double d2x = p4.x - p3.x;
    double d2y = p4.y - p3.y;

    double denom = d1x * d2y - d1y * d2x;
    if (std::abs(denom) < 1e-9) return false;

    double invDenom = 1.0 / denom;
    double u = ((p3.x - p1.x) * d2y - (p3.y - p1.y) * d2x) * invDenom;
    double v = ((p3.x - p1.x) * d1y - (p3.y - p1.y) * d1x) * invDenom;

    if (inclusive) {
        if (u >= -1e-5 && u <= 1.0 + 1e-5 && v >= -1e-5 && v <= 1.0 + 1e-5) {
            outHit.x = p1.x + u * d1x;
            outHit.y = p1.y + u * d1y;
            return true;
        }
    } else {
        if (u > 1e-4 && u < (1.0 - 1e-4) && v > 1e-4 && v < (1.0 - 1e-4)) {
            outHit.x = p1.x + u * d1x;
            outHit.y = p1.y + u * d1y;
            return true;
        }
    }
    return false;
}

void PruneSwallowtailLoops(std::vector<Point2D>& rail, size_t maxLoopSpan) {
    if (rail.size() < 4) return;

    size_t i = 0;
    while (i + 2 < rail.size()) {
        bool found = false;
        size_t maxJ = std::min(rail.size() - 2, i + maxLoopSpan);
        for (size_t j = maxJ; j >= i + 2; --j) {
            Point2D hit;
            if (IntersectSegments(rail[i], rail[i + 1], rail[j], rail[j + 1], hit, /*inclusive=*/true)) {
                rail[i + 1] = hit;
                rail.erase(rail.begin() + (i + 2), rail.begin() + (j + 1));
                found = true;
                break;
            }
            if (j == i + 2) break;
        }
        if (!found) {
            ++i;
        }
    }
}

void DetectSelfIntersections(const std::vector<Point2D>& leftRail,
                             const std::vector<Point2D>& rightRail,
                             bool isClosedPolygon,
                             std::vector<Point2D>& outIntersections) {
    outIntersections.clear();
    if (leftRail.empty() || rightRail.empty()) return;

    auto deduplicateAndAdd = [&](const Point2D& hit) {
        for (const auto& existing : outIntersections) {
            if (std::hypot(existing.x - hit.x, existing.y - hit.y) < 1e-3) {
                return;
            }
        }
        outIntersections.push_back(hit);
    };

    if (!isClosedPolygon) {
        // Build closed boundary polygon ring: leftRail forward, then rightRail backward
        std::vector<Point2D> poly;
        poly.reserve(leftRail.size() + rightRail.size());
        for (const auto& p : leftRail) poly.push_back(p);
        for (size_t i = rightRail.size(); i > 0; --i) poly.push_back(rightRail[i - 1]);

        const size_t M = poly.size();
        if (M < 4) return;

        for (size_t i = 0; i < M; ++i) {
            const Point2D& p1 = poly[i];
            const Point2D& p2 = poly[(i + 1) % M];

            for (size_t j = i + 1; j < M; ++j) {
                if (j == i || j == (i + 1) % M || i == (j + 1) % M) continue;
                const Point2D& p3 = poly[j];
                const Point2D& p4 = poly[(j + 1) % M];

                Point2D hit;
                if (IntersectSegments(p1, p2, p3, p4, hit, /*inclusive=*/false)) {
                    deduplicateAndAdd(hit);
                }
            }
        }
    } else {
        // Closed polygon: outer loop (leftRail) and inner loop (rightRail)
        auto testRing = [&](const std::vector<Point2D>& ring) {
            const size_t R = ring.size();
            if (R < 4) return;
            for (size_t i = 0; i < R; ++i) {
                const Point2D& p1 = ring[i];
                const Point2D& p2 = ring[(i + 1) % R];
                for (size_t j = i + 1; j < R; ++j) {
                    if (j == i || j == (i + 1) % R || i == (j + 1) % R) continue;
                    const Point2D& p3 = ring[j];
                    const Point2D& p4 = ring[(j + 1) % R];
                    Point2D hit;
                    if (IntersectSegments(p1, p2, p3, p4, hit, /*inclusive=*/false)) {
                        deduplicateAndAdd(hit);
                    }
                }
            }
        };

        testRing(leftRail);
        testRing(rightRail);

        // Also test outer edges against inner edges
        const size_t L = leftRail.size();
        const size_t R = rightRail.size();
        for (size_t i = 0; i < L; ++i) {
            const Point2D& p1 = leftRail[i];
            const Point2D& p2 = leftRail[(i + 1) % L];
            for (size_t j = 0; j < R; ++j) {
                const Point2D& p3 = rightRail[j];
                const Point2D& p4 = rightRail[(j + 1) % R];
                Point2D hit;
                if (IntersectSegments(p1, p2, p3, p4, hit, /*inclusive=*/false)) {
                    deduplicateAndAdd(hit);
                }
            }
        }
    }
}

std::vector<size_t> DetectRetrogradeSteps(const std::vector<Point2D>& sidePts,
                                         std::span<const SegmentInfo> segs,
                                         bool isRight) {
    std::vector<size_t> retrogradeIndices;
    if (sidePts.size() < 2 || segs.empty()) return retrogradeIndices;

    for (size_t k = 1; k < sidePts.size(); ++k) {
        double dx = sidePts[k].x - sidePts[k - 1].x;
        double dy = sidePts[k].y - sidePts[k - 1].y;
        double bestDot = -1e9;
        for (const auto& s : segs) {
            double dot = dx * s.ux + dy * s.uy;
            if (dot > bestDot) bestDot = dot;
        }
        if (bestDot < -1e-5) {
            size_t encodedIdx = k | (isRight ? (size_t(1) << 60) : 0);
            retrogradeIndices.push_back(encodedIdx);
        }
    }
    return retrogradeIndices;
}

} // namespace InkInternal
} // namespace Folio
