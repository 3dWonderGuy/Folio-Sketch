#include "folio_sketch/ink_generator.hpp"
#include "ink_generator_internal.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Folio::InkInternal;

namespace {

// =============================================================================
// CORE SINGLE RIBBON BUILDER (Open ribbon or closed polygon loop)
// =============================================================================

BLPath BuildSingleRibbon(const std::vector<StrokePoint>& cleanPts, CapType capType, bool isClosedPolygon) {
    BLPath path;
    if (cleanPts.empty()) return path;
    if (cleanPts.size() == 1) return InkGenerator::TraceCap(cleanPts[0], capType);

    const size_t N = cleanPts.size();
    bool isClosed = isClosedPolygon && (N >= 4) &&
                    (std::hypot(cleanPts.back().x - cleanPts.front().x,
                                cleanPts.back().y - cleanPts.front().y) < 1e-4);
    const size_t M = isClosed ? (N - 1) : N;

    // Precompute segment unit tangents and normals
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

    // Monotonic guard: rejects or clamps backward steps against the local stroke tangent
    auto pushMonotonic = [](std::vector<Point2D>& sidePts, Point2D pt, double tX, double tY) {
        if (sidePts.empty()) {
            sidePts.push_back(pt);
            return;
        }
        double dx = pt.x - sidePts.back().x;
        double dy = pt.y - sidePts.back().y;
        double forward = dx * tX + dy * tY;
        if (forward < 0.0) {
            pt.x -= forward * tX;
            pt.y -= forward * tY;
        }
        if (std::hypot(pt.x - sidePts.back().x, pt.y - sidePts.back().y) >= 1e-5) {
            sidePts.push_back(pt);
        }
    };

    // Lambda to process an interior or closed-loop corner vertex
    auto processCorner = [&](size_t i, const SegmentInfo& sIn, const SegmentInfo& sOut) {
        const auto& Pi = cleanPts[i];
        double r = std::max(0.0f, Pi.r);

        // Compute local average stroke tangent for monotonic verification
        double avgTanX = sIn.ux + sOut.ux;
        double avgTanY = sIn.uy + sOut.uy;
        double tanLen = std::hypot(avgTanX, avgTanY);
        if (tanLen > 1e-6) {
            avgTanX /= tanLen;
            avgTanY /= tanLen;
        } else {
            avgTanX = sOut.ux;
            avgTanY = sOut.uy;
        }

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
            pushMonotonic(leftPts,  { Pi.x + offL.x, Pi.y + offL.y }, avgTanX, avgTanY);
            pushMonotonic(rightPts, { Pi.x + offR.x, Pi.y + offR.y }, avgTanX, avgTanY);
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
                    pushMonotonic(outPts, { Pi.x + inOff.x, Pi.y + inOff.y }, avgTanX, avgTanY);
                    return;
                }

                if (isInner) {
                    // Inner corner: single midpoint prevents retrograde crossing
                    pushMonotonic(outPts, { Pi.x + (inOff.x + outOff.x) * 0.5, Pi.y + (inOff.y + outOff.y) * 0.5 }, avgTanX, avgTanY);
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

                    pushMonotonic(outPts, { Pi.x + inOff.x, Pi.y + inOff.y }, avgTanX, avgTanY);
                    pushMonotonic(outPts, { Pi.x + verts[chosenMid].x, Pi.y + verts[chosenMid].y }, avgTanX, avgTanY);
                    pushMonotonic(outPts, { Pi.x + outOff.x, Pi.y + outOff.y }, avgTanX, avgTanY);
                } else {
                    pushMonotonic(outPts, { Pi.x + inOff.x, Pi.y + inOff.y }, avgTanX, avgTanY);
                    pushMonotonic(outPts, { Pi.x + outOff.x, Pi.y + outOff.y }, avgTanX, avgTanY);
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
        // Precise segment-clamped bisector miter join with midpoint bevel fallback
        auto emitInner = [&](std::vector<Point2D>& inPts) {
            double sumInX = offIn_inner.x + offOut_inner.x;
            double sumInY = offIn_inner.y + offOut_inner.y;
            double bInLen = std::hypot(sumInX, sumInY);

            if (bInLen < 1e-6) {
                // Hairpin turnaround: inner corner is the pivot point at Pi
                pushMonotonic(inPts, { Pi.x, Pi.y }, avgTanX, avgTanY);
                return;
            }

            double bInX = sumInX / bInLen;
            double bInY = sumInY / bInLen;

            double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
            double sinHalf = std::sqrt(std::max(0.0, (1.0 - dot) * 0.5));

            double safeLimit = (std::min(sIn.len, sOut.len) * 0.5) / std::max(1e-4, sinHalf);

            double innerDist = r;
            if (cosHalf > 1e-4) {
                innerDist = std::min(r / cosHalf, 1.4 * r);
            }

            if (innerDist > safeLimit) {
                // Miter overshoots segment length: fall back to midpoint bevel
                Point2D bevelPt = { Pi.x + (offIn_inner.x + offOut_inner.x) * 0.5,
                                    Pi.y + (offIn_inner.y + offOut_inner.y) * 0.5 };
                pushMonotonic(inPts, bevelPt, avgTanX, avgTanY);
            } else {
                Point2D miterPt = { Pi.x + bInX * innerDist, Pi.y + bInY * innerDist };
                pushMonotonic(inPts, miterPt, avgTanX, avgTanY);
            }
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
                Point2D arcPt = { Pi.x + std::cos(a) * r, Pi.y + std::sin(a) * r };
                pushMonotonic(outPts, arcPt, avgTanX, avgTanY);
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
        pushMonotonic(leftPts,  { cleanPts.back().x + offL_end.x, cleanPts.back().y + offL_end.y }, lastSeg.ux, lastSeg.uy);
        pushMonotonic(rightPts, { cleanPts.back().x + offR_end.x, cleanPts.back().y + offR_end.y }, lastSeg.ux, lastSeg.uy);
    }

    if (leftPts.empty() || rightPts.empty()) return path;

    // Extrude single continuous closed contour with head/tail endcaps
    path.move_to(leftPts[0].x, leftPts[0].y);

    for (size_t i = 1; i < leftPts.size(); ++i) {
        path.line_to(leftPts[i].x, leftPts[i].y);
    }

    // Tail Cap: leftPts.back() -> rightPts.back() around cleanPts.back()
    InkGenerator::TraceCap(path, cleanPts.back(), leftPts.back(), rightPts.back(), capType, /*isTail=*/true);

    if (rightPts.size() > 1) {
        for (size_t i = rightPts.size() - 1; i > 0; --i) {
            path.line_to(rightPts[i - 1].x, rightPts[i - 1].y);
        }
    }

    // Head Cap: rightPts[0] -> leftPts[0] around cleanPts.front()
    InkGenerator::TraceCap(path, cleanPts.front(), leftPts[0], rightPts[0], capType, /*isTail=*/false);

    path.close();
    return path;
}

} // namespace

// =============================================================================
// CONTINUOUS SOLID RIBBON TESSELLATION (Public Entry Point)
// =============================================================================

BLPath InkGenerator::BuildSolidRibbon(const std::vector<StrokePoint>& pts, CapType capType, bool isClosedPolygon) {
    BLPath path;
    if (pts.empty()) return path;
    if (pts.size() == 1) return TraceCap(pts[0], capType);

    // 1. Disk-Containment & Radius-Proportional Pre-Filter
    std::vector<StrokePoint> cleanPts;
    cleanPts.reserve(pts.size());

    for (size_t idx = 0; idx < pts.size(); ++idx) {
        const auto& p = pts[idx];
        if (cleanPts.empty()) {
            cleanPts.push_back(p);
            continue;
        }

        auto& prev = cleanPts.back();
        double d = std::hypot(p.x - prev.x, p.y - prev.y);
        double maxR = std::max(static_cast<double>(p.r), static_cast<double>(prev.r));

        // 1a. Disk containment: new point engulfed by previous kept point
        if (d + p.r <= prev.r + maxR * 0.05) {
            prev.pressure = std::max(prev.pressure, p.pressure);
            prev.r = std::max(prev.r, p.r);
            continue;
        }

        // 1b. Disk containment: previous kept point engulfed by new point's larger disk
        if (d + prev.r <= p.r + maxR * 0.05) {
            if (cleanPts.size() == 1) {
                prev = p;
            } else {
                cleanPts.back() = p;
            }
            continue;
        }

        // 1c. Radius-proportional minimum step for interior points
        bool isLast = (idx + 1 == pts.size());
        double minStep = std::max(1e-3, maxR * 0.10);
        if (!isLast && d < minStep) {
            bool isTurn = false;
            if (idx + 1 < pts.size() && d > 1e-6) {
                double dxIn = p.x - prev.x, dyIn = p.y - prev.y;
                double dxOut = pts[idx + 1].x - p.x, dyOut = pts[idx + 1].y - p.y;
                double lenOut = std::hypot(dxOut, dyOut);
                if (lenOut > 1e-6) {
                    double cosTurn = (dxIn * dxOut + dyIn * dyOut) / (d * lenOut);
                    if (cosTurn < 0.96) {
                        isTurn = true;
                    }
                }
            }
            if (!isTurn) {
                prev.pressure = std::max(prev.pressure, p.pressure);
                prev.r = std::max(prev.r, p.r);
                continue;
            }
        }

        cleanPts.push_back(p);
    }

    if (cleanPts.size() <= 1) {
        return TraceCap(cleanPts.empty() ? pts[0] : cleanPts[0], capType);
    }

    // 1b. Regularize radius rate-of-change: |dr/ds| <= maxSlope
    if (cleanPts.size() >= 2) {
        constexpr double maxSlope = 0.85;
        // Forward pass
        for (size_t i = 1; i < cleanPts.size(); ++i) {
            double d = std::hypot(cleanPts[i].x - cleanPts[i - 1].x,
                                  cleanPts[i].y - cleanPts[i - 1].y);
            double maxAllowedR = cleanPts[i - 1].r + d * maxSlope;
            if (cleanPts[i].r > maxAllowedR) {
                cleanPts[i].r = static_cast<float>(maxAllowedR);
            }
        }
        // Backward pass
        for (size_t i = cleanPts.size() - 1; i > 0; --i) {
            double d = std::hypot(cleanPts[i - 1].x - cleanPts[i].x,
                                  cleanPts[i - 1].y - cleanPts[i].y);
            double maxAllowedR = cleanPts[i].r + d * maxSlope;
            if (cleanPts[i - 1].r > maxAllowedR) {
                cleanPts[i - 1].r = static_cast<float>(maxAllowedR);
            }
        }
    }

    if (isClosedPolygon) {
        if (cleanPts.size() >= 3) {
            if (std::hypot(cleanPts.back().x - cleanPts.front().x,
                           cleanPts.back().y - cleanPts.front().y) >= 1e-4) {
                cleanPts.push_back(cleanPts.front());
            }
        }
        return BuildSingleRibbon(cleanPts, capType, true);
    }

    // -------------------------------------------------------------------------
    // 3. HAIRPIN TURN SPLITTING (Open Strokes Only)
    // Detect sharp reversals (cosTheta < -0.70, i.e. turn > ~135°)
    // -------------------------------------------------------------------------
    std::vector<size_t> splitIndices;
    if (cleanPts.size() >= 3) {
        for (size_t i = 1; i < cleanPts.size() - 1; ++i) {
            double dxIn  = cleanPts[i].x - cleanPts[i - 1].x;
            double dyIn  = cleanPts[i].y - cleanPts[i - 1].y;
            double lenIn = std::hypot(dxIn, dyIn);

            double dxOut  = cleanPts[i + 1].x - cleanPts[i].x;
            double dyOut  = cleanPts[i + 1].y - cleanPts[i].y;
            double lenOut = std::hypot(dxOut, dyOut);

            if (lenIn > 1e-7 && lenOut > 1e-7) {
                double cosTheta = (dxIn * dxOut + dyIn * dyOut) / (lenIn * lenOut);
                if (cosTheta < -0.70) {
                    splitIndices.push_back(i);
                }
            }
        }
    }

    if (splitIndices.empty()) {
        return BuildSingleRibbon(cleanPts, capType, false);
    }

    BLPath combinedPath;
    size_t subStart = 0;
    for (size_t splitIdx : splitIndices) {
        if (splitIdx > subStart) {
            std::vector<StrokePoint> subPts(cleanPts.begin() + subStart, cleanPts.begin() + splitIdx + 1);
            BLPath subRibbon = BuildSingleRibbon(subPts, capType, false);
            if (!subRibbon.is_empty()) {
                combinedPath.add_path(subRibbon);
            }
            subStart = splitIdx;
        }
    }

    if (subStart < cleanPts.size() - 1) {
        std::vector<StrokePoint> subPts(cleanPts.begin() + subStart, cleanPts.end());
        BLPath subRibbon = BuildSingleRibbon(subPts, capType, false);
        if (!subRibbon.is_empty()) {
            combinedPath.add_path(subRibbon);
        }
    }

    return combinedPath;
}
