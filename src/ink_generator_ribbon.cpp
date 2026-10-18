#include "folio_sketch/ink_generator.hpp"
#include "ink_generator_internal.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace Folio::InkInternal;

// =============================================================================
// CONTINUOUS SOLID RIBBON TESSELLATION
// =============================================================================

BLPath InkGenerator::BuildSolidRibbon(const std::vector<StrokePoint>& pts, CapType capType, bool isClosedPolygon) {
    BLPath path;
    if (pts.empty()) return path;
    if (pts.size() == 1) return TraceCap(pts[0], capType);

    // 1. Pre-filter: collapse consecutive duplicate/stationary points (< 1e-4 mm)
    std::vector<StrokePoint> cleanPts;
    cleanPts.reserve(pts.size());
    for (const auto& p : pts) {
        if (cleanPts.empty() || std::hypot(p.x - cleanPts.back().x, p.y - cleanPts.back().y) >= 1e-4) {
            cleanPts.push_back(p);
        } else {
            cleanPts.back().r = std::max(cleanPts.back().r, p.r);
            cleanPts.back().pressure = std::max(cleanPts.back().pressure, p.pressure);
        }
    }

    if (cleanPts.size() <= 1) {
        return TraceCap(cleanPts.empty() ? pts[0] : cleanPts[0], capType);
    }

    // 1b. Regularize radius rate-of-change: |dr/ds| <= maxSlope
    // Prevents self-intersecting retrograde polygon spikes when pressure changes rapidly over short distances
    if (cleanPts.size() >= 2) {
        constexpr double maxSlope = 0.85;
        // Forward pass
        for (size_t i = 1; i < cleanPts.size(); ++i) {
            double d = std::hypot(cleanPts[i].x - cleanPts[i - 1].x,
                                  cleanPts[i].y - cleanPts[i - 1].y);
            double maxR = cleanPts[i - 1].r + d * maxSlope;
            if (cleanPts[i].r > maxR) {
                cleanPts[i].r = static_cast<float>(maxR);
            }
        }
        // Backward pass
        for (size_t i = cleanPts.size() - 1; i > 0; --i) {
            double d = std::hypot(cleanPts[i - 1].x - cleanPts[i].x,
                                  cleanPts[i - 1].y - cleanPts[i].y);
            double maxR = cleanPts[i].r + d * maxSlope;
            if (cleanPts[i - 1].r > maxR) {
                cleanPts[i - 1].r = static_cast<float>(maxR);
            }
        }
    }

    if (isClosedPolygon && cleanPts.size() >= 3) {
        if (std::hypot(cleanPts.back().x - cleanPts.front().x,
                       cleanPts.back().y - cleanPts.front().y) >= 1e-4) {
            cleanPts.push_back(cleanPts.front());
        }
    }

    const size_t N = cleanPts.size();
    bool isClosed = isClosedPolygon && (N >= 4) &&
                    (std::hypot(cleanPts.back().x - cleanPts.front().x,
                                cleanPts.back().y - cleanPts.front().y) < 1e-4);
    const size_t M = isClosed ? (N - 1) : N;

    // 2. Precompute segment unit tangents and normals
    struct SegmentInfo {
        double len = 0.0;
        double ux = 1.0;
        double uy = 0.0;
        double nx = 0.0;
        double ny = 1.0;
    };

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

    std::vector<Point2D> leftPts;
    std::vector<Point2D> rightPts;
    leftPts.reserve(N * 2);
    rightPts.reserve(N * 2);

    // Lambda to process an interior or closed-loop corner vertex
    auto processCorner = [&](size_t i, const SegmentInfo& sIn, const SegmentInfo& sOut) {
        const auto& Pi = cleanPts[i];
        double r = std::max(0.0f, Pi.r);

        Point2D offL_in, offR_in, offL_out, offR_out;
        GetRibbonOffsets(capType, r, sIn.nx, sIn.ny, offL_in, offR_in);
        GetRibbonOffsets(capType, r, sOut.nx, sOut.ny, offL_out, offR_out);

        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
        double turnAngle = std::atan2(std::abs(cross), dot);

        if (turnAngle < 0.40) {
            // Continuous smooth curve: single point on each side along unit average normal
            double avgNx = sIn.nx + sOut.nx;
            double avgNy = sIn.ny + sOut.ny;
            double avgLen = std::hypot(avgNx, avgNy);
            if (avgLen > 1e-6) {
                avgNx /= avgLen;
                avgNy /= avgLen;
            } else {
                avgNx = sIn.nx;
                avgNy = sIn.ny;
            }
            Point2D offL, offR;
            GetRibbonOffsets(capType, r, avgNx, avgNy, offL, offR);
            leftPts.push_back({ Pi.x + offL.x, Pi.y + offL.y });
            rightPts.push_back({ Pi.x + offR.x, Pi.y + offR.y });
            return;
        }

        // In screen coordinates (+Y down), cross > 0 is clockwise (RIGHT turn).
        // Therefore, cross < 0 is counter-clockwise (LEFT turn, left is inner).
        bool leftIsInner = (cross < 0.0) || (cross == 0.0 && dot < 0.0);

        // Polygonal nibs (Square/Highlighter, Chisel/Calligraphy):
        if (capType == CapType::Square || capType == CapType::Chisel) {
            auto verts = GetNibVertices(capType, r);
            const int K = static_cast<int>(verts.size());

            auto findIdx = [&](const Point2D& off) -> int {
                int best = 0;
                double minD = 1e9;
                for (int k = 0; k < K; ++k) {
                    double d = std::hypot(verts[k].x - off.x, verts[k].y - off.y);
                    if (d < minD) { minD = d; best = k; }
                }
                return best;
            };

            double uElbowX = sIn.ux - sOut.ux;
            double uElbowY = sIn.uy - sOut.uy;
            double uElbowLen = std::hypot(uElbowX, uElbowY);
            if (uElbowLen > 1e-6) {
                uElbowX /= uElbowLen;
                uElbowY /= uElbowLen;
            } else {
                uElbowX = sIn.ux;
                uElbowY = sIn.uy;
            }

            auto emitNibSide = [&](bool isInner, const Point2D& inOff, const Point2D& outOff, std::vector<Point2D>& outPts) {
                int idxIn = findIdx(inOff);
                int idxOut = findIdx(outOff);

                if (idxIn == idxOut) {
                    outPts.push_back({ Pi.x + inOff.x, Pi.y + inOff.y });
                    return;
                }

                if (isInner) {
                    // Inner corner: single midpoint prevents retrograde crossing
                    outPts.push_back({ Pi.x + (inOff.x + outOff.x) * 0.5, Pi.y + (inOff.y + outOff.y) * 0.5 });
                    return;
                }

                // Check difference around perimeter (K=4)
                int diff = (idxOut - idxIn + K) % K;
                if (diff == 2) {
                    // Opposite diagonal corners: traverse through outward intermediate corner
                    int midCW  = (idxIn + 1) % K;
                    int midCCW = (idxIn + K - 1) % K;

                    double dotCW  = verts[midCW].x * uElbowX + verts[midCW].y * uElbowY;
                    double dotCCW = verts[midCCW].x * uElbowX + verts[midCCW].y * uElbowY;

                    int chosenMid = (dotCW >= dotCCW) ? midCW : midCCW;

                    outPts.push_back({ Pi.x + inOff.x, Pi.y + inOff.y });
                    outPts.push_back({ Pi.x + verts[chosenMid].x, Pi.y + verts[chosenMid].y });
                    outPts.push_back({ Pi.x + outOff.x, Pi.y + outOff.y });
                } else {
                    outPts.push_back({ Pi.x + inOff.x, Pi.y + inOff.y });
                    outPts.push_back({ Pi.x + outOff.x, Pi.y + outOff.y });
                }
            };

            emitNibSide(leftIsInner, offL_in, offL_out, leftPts);
            emitNibSide(!leftIsInner, offR_in, offR_out, rightPts);
            return;
        }

        const Point2D& offIn_inner  = leftIsInner ? offL_in  : offR_in;
        const Point2D& offOut_inner = leftIsInner ? offL_out : offR_out;
        const Point2D& offIn_outer  = leftIsInner ? offR_in  : offL_in;

        // 1. INNER CORNER:
        // Precise monotonic bisector miter join without retrograde folding or self-intersecting loops
        auto emitInner = [&](std::vector<Point2D>& inPts) {
            double sumInX = offIn_inner.x + offOut_inner.x;
            double sumInY = offIn_inner.y + offOut_inner.y;
            double bInLen = std::hypot(sumInX, sumInY);

            if (bInLen < 1e-6) {
                // Hairpin 180 turnaround: inner corner is the pivot point at Pi
                inPts.push_back({ Pi.x, Pi.y });
                return;
            }

            double bInX = sumInX / bInLen;
            double bInY = sumInY / bInLen;

            double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));

            double innerDist = r;
            if (cosHalf > 1e-4) {
                innerDist = std::min(r / cosHalf, 1.4 * r);
            }

            inPts.push_back({ Pi.x + bInX * innerDist, Pi.y + bInY * innerDist });
        };

        // 2. OUTER CORNER: Smooth circular arc for sharp turns
        auto emitOuter = [&](std::vector<Point2D>& outPts) {
            int steps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 16.0))), 2, 24);
            double aStart = std::atan2(offIn_outer.y, offIn_outer.x);
            // Left turn (leftIsInner): right is outer, sweeps CCW (-turnAngle)
            // Right turn (!leftIsInner): left is outer, sweeps CW (+turnAngle)
            double sweep = leftIsInner ? -turnAngle : turnAngle;

            for (int s = 0; s <= steps; ++s) {
                double t = static_cast<double>(s) / steps;
                double a = aStart + t * sweep;
                outPts.push_back({ Pi.x + std::cos(a) * r, Pi.y + std::sin(a) * r });
            }
        };

        if (leftIsInner) {
            emitInner(leftPts);
            emitOuter(rightPts);
        } else {
            emitOuter(leftPts);
            emitInner(rightPts);
        }
    };

    if (isClosed) {
        // ---------------------------------------------------------------------
        // CLOSED POLYGON LOOP (Seamless ring without terminal endcaps)
        // ---------------------------------------------------------------------
        for (size_t i = 0; i < M; ++i) {
            const auto& sIn  = (i == 0) ? segs[M - 1] : segs[i - 1];
            const auto& sOut = segs[i];
            processCorner(i, sIn, sOut);
        }

        if (leftPts.empty() || rightPts.empty()) return path;

        // Outer contour (CW)
        path.move_to(leftPts[0].x, leftPts[0].y);
        for (size_t i = 1; i < leftPts.size(); ++i) {
            path.line_to(leftPts[i].x, leftPts[i].y);
        }
        path.close();

        // Inner contour in reverse winding direction (CCW)
        path.move_to(rightPts.back().x, rightPts.back().y);
        for (size_t i = rightPts.size() - 1; i > 0; --i) {
            path.line_to(rightPts[i - 1].x, rightPts[i - 1].y);
        }
        path.close();

        return path;
    }

    // -------------------------------------------------------------------------
    // OPEN STROKE RIBBON (Honors endcaps at P0 and P_{N-1})
    // -------------------------------------------------------------------------

    // Start point P0
    {
        Point2D offL0, offR0;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts[0].r), segs[0].nx, segs[0].ny, offL0, offR0);
        leftPts.push_back({ cleanPts[0].x + offL0.x, cleanPts[0].y + offL0.y });
        rightPts.push_back({ cleanPts[0].x + offR0.x, cleanPts[0].y + offR0.y });
    }

    // Interior vertices Pi (i = 1 .. N - 2)
    for (size_t i = 1; i < N - 1; ++i) {
        processCorner(i, segs[i - 1], segs[i]);
    }

    // End point P_{N-1}
    {
        const auto& lastSeg = segs.back();
        Point2D offL_end, offR_end;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts.back().r), lastSeg.nx, lastSeg.ny, offL_end, offR_end);
        leftPts.push_back({ cleanPts.back().x + offL_end.x, cleanPts.back().y + offL_end.y });
        rightPts.push_back({ cleanPts.back().x + offR_end.x, cleanPts.back().y + offR_end.y });
    }

    // Extrude single continuous closed contour with head/tail endcaps
    path.move_to(leftPts[0].x, leftPts[0].y);

    for (size_t i = 1; i < leftPts.size(); ++i) {
        path.line_to(leftPts[i].x, leftPts[i].y);
    }

    // Tail Cap: leftPts.back() -> rightPts.back() around cleanPts.back()
    TraceCap(path, cleanPts.back(), leftPts.back(), rightPts.back(), capType, /*isTail=*/true);

    if (rightPts.size() > 1) {
        for (size_t i = rightPts.size() - 1; i > 0; --i) {
            path.line_to(rightPts[i - 1].x, rightPts[i - 1].y);
        }
    }

    // Head Cap: rightPts[0] -> leftPts[0] around cleanPts.front()
    TraceCap(path, cleanPts.front(), leftPts[0], rightPts[0], capType, /*isTail=*/false);

    path.close();
    return path;
}
