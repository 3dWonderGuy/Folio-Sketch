#include "folio_sketch/ink_generator.hpp"
#include "ink_generator_internal.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace Folio::InkInternal;

// =============================================================================
// CONTINUOUS SOLID RIBBON TESSELLATION
// =============================================================================

BLPath InkGenerator::BuildSolidRibbon(const std::vector<StrokePoint>& pts, CapType capType) {
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

    const size_t N = cleanPts.size();

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
            segs[i].nx = -segs[i].uy;
            segs[i].ny =  segs[i].ux;
        } else {
            if (i > 0) {
                segs[i] = segs[i - 1];
            } else {
                segs[i].ux = 1.0;
                segs[i].uy = 0.0;
                segs[i].nx = 0.0;
                segs[i].ny = 1.0;
            }
        }
    }

    std::vector<Point2D> leftPts;
    std::vector<Point2D> rightPts;
    leftPts.reserve(N * 2);
    rightPts.reserve(N * 2);

    // 3. Start point P0
    {
        Point2D offL0, offR0;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts[0].r), segs[0].nx, segs[0].ny, offL0, offR0);
        leftPts.push_back({ cleanPts[0].x + offL0.x, cleanPts[0].y + offL0.y });
        rightPts.push_back({ cleanPts[0].x + offR0.x, cleanPts[0].y + offR0.y });
    }

    // 4. Interior vertices Pi (i = 1 .. N - 2):
    for (size_t i = 1; i < N - 1; ++i) {
        const auto& sIn = segs[i - 1];
        const auto& sOut = segs[i];
        const auto& Pi = cleanPts[i];
        double r = std::max(0.0f, Pi.r);

        Point2D offL_in, offR_in, offL_out, offR_out;
        GetRibbonOffsets(capType, r, sIn.nx, sIn.ny, offL_in, offR_in);
        GetRibbonOffsets(capType, r, sOut.nx, sOut.ny, offL_out, offR_out);

        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
        double turnAngle = std::atan2(std::abs(cross), dot);

        if (std::abs(cross) < 1e-4 && dot > 0.0) {
            // Collinear: smooth continuous offset
            leftPts.push_back({ Pi.x + offL_in.x, Pi.y + offL_in.y });
            rightPts.push_back({ Pi.x + offR_in.x, Pi.y + offR_in.y });
            continue;
        }

        bool leftIsInner = (cross > 0.0) || (cross == 0.0 && dot < 0.0);

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

                if (isInner && dot >= -0.85) {
                    // Moderate inner corner: bevel midpoint between facets
                    outPts.push_back({ Pi.x + (inOff.x + outOff.x) * 0.5, Pi.y + (inOff.y + outOff.y) * 0.5 });
                    return;
                }

                // Check difference around perimeter (K=4)
                int diff = (idxOut - idxIn + K) % K;
                if (diff == 2) {
                    // Opposite diagonal corners: MUST traverse perimeter through the outward intermediate corner
                    // to prevent diagonal interior cuts, double-fang artifacts, and inverted V-notches!
                    int midCW  = (idxIn + 1) % K;
                    int midCCW = (idxIn + K - 1) % K;

                    double dotCW  = verts[midCW].x * uElbowX + verts[midCW].y * uElbowY;
                    double dotCCW = verts[midCCW].x * uElbowX + verts[midCCW].y * uElbowY;

                    int chosenMid = (isInner ? (dotCW < dotCCW) : (dotCW >= dotCCW)) ? midCW : midCCW;

                    outPts.push_back({ Pi.x + inOff.x, Pi.y + inOff.y });
                    outPts.push_back({ Pi.x + verts[chosenMid].x, Pi.y + verts[chosenMid].y });
                    outPts.push_back({ Pi.x + outOff.x, Pi.y + outOff.y });
                } else {
                    // Adjacent corners along the perimeter: connect along the facet edge
                    outPts.push_back({ Pi.x + inOff.x, Pi.y + inOff.y });
                    outPts.push_back({ Pi.x + outOff.x, Pi.y + outOff.y });
                }
            };

            emitNibSide(leftIsInner, offL_in, offL_out, leftPts);
            emitNibSide(!leftIsInner, offR_in, offR_out, rightPts);
            continue;
        }

        const Point2D& offIn_inner  = leftIsInner ? offL_in  : offR_in;
        const Point2D& offOut_inner = leftIsInner ? offL_out : offR_out;
        const Point2D& offIn_outer  = leftIsInner ? offR_in  : offL_in;

        // 1. INNER CORNER:
        // Smooth bisector miter for gentle turns, 3-point circular fillet for 90-deg corners (extra roundness),
        // and direct bevel fallback for short segments / hairpin reversals (no spikes).
        auto emitInner = [&](std::vector<Point2D>& inPts) {
            double sumInX = offIn_inner.x + offOut_inner.x;
            double sumInY = offIn_inner.y + offOut_inner.y;
            double bInLen = std::hypot(sumInX, sumInY);

            double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
            double sinHalf = std::sqrt(std::max(0.0, (1.0 - dot) * 0.5));

            // Projection along incoming/outgoing segment for bisector miter:
            // proj = r * tan(theta / 2) = r * (sinHalf / cosHalf)
            double proj = (cosHalf > 1e-4) ? (r * (sinHalf / cosHalf)) : 1e9;
            double maxAllowedProj = 0.85 * std::min(sIn.len, sOut.len);

            if (dot >= 0.5 && proj <= maxAllowedProj && bInLen > 1e-6) {
                // Gentle turns (<= 60 deg): smooth bisector miter (miter ratio <= 1.155)
                double bInX = sumInX / bInLen;
                double bInY = sumInY / bInLen;
                double innerDist = r / cosHalf;
                inPts.push_back({ Pi.x + bInX * innerDist, Pi.y + bInY * innerDist });
            } else if (dot >= -0.85 && proj <= maxAllowedProj && bInLen > 1e-6) {
                // Moderate to sharp turns (60 deg to 150 deg, including 90 deg corners):
                // Smooth 3-point circular fillet of radius r eliminating miter spikes and adding extra roundness!
                double bInX = sumInX / bInLen;
                double bInY = sumInY / bInLen;
                inPts.push_back({ Pi.x + offIn_inner.x, Pi.y + offIn_inner.y });
                inPts.push_back({ Pi.x + bInX * r, Pi.y + bInY * r });
                inPts.push_back({ Pi.x + offOut_inner.x, Pi.y + offOut_inner.y });
            } else {
                // Short segments (dense samples) or extreme hairpin turnaround (> 150 deg):
                // Direct bevel join between inner offsets (projection along segment is exactly 0, prevents spikes).
                inPts.push_back({ Pi.x + offIn_inner.x, Pi.y + offIn_inner.y });
                double dInner = std::hypot(offIn_inner.x - offOut_inner.x, offIn_inner.y - offOut_inner.y);
                if (dInner >= 1e-4) {
                    inPts.push_back({ Pi.x + offOut_inner.x, Pi.y + offOut_inner.y });
                }
            }
        };

        // 2. OUTER CORNER: Extra smooth circular arc
        auto emitOuter = [&](std::vector<Point2D>& outPts) {
            if (turnAngle < 0.03) {
                // Subpixel turn / nearly straight: single collinear offset
                outPts.push_back({ Pi.x + offIn_outer.x, Pi.y + offIn_outer.y });
                return;
            }

            // High-fidelity smooth arc (4 to 32 steps based on angle) for continuous silky roundness
            int steps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 24.0))), 4, 32);
            double aStart = std::atan2(offIn_outer.y, offIn_outer.x);
            // Bulletproof sweep: left turn sweeps +turnAngle CCW, right turn sweeps -turnAngle CW
            double sweep = leftIsInner ? turnAngle : -turnAngle;

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
    }

    // 5. End point P_{N-1}
    {
        const auto& lastSeg = segs.back();
        Point2D offL_end, offR_end;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts.back().r), lastSeg.nx, lastSeg.ny, offL_end, offR_end);
        leftPts.push_back({ cleanPts.back().x + offL_end.x, cleanPts.back().y + offL_end.y });
        rightPts.push_back({ cleanPts.back().x + offR_end.x, cleanPts.back().y + offR_end.y });
    }

    // 6. Extrude single continuous closed contour with head/tail endcaps
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
