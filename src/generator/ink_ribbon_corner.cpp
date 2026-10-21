#include "ink_ribbon_corner.hpp"
#include "ink_generator_internal.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {
namespace InkInternal {

void PushMonotonic(std::vector<Point2D>& sidePts, Point2D pt) {
    if (sidePts.empty()) {
        sidePts.push_back(pt);
        return;
    }
    if (std::hypot(pt.x - sidePts.back().x, pt.y - sidePts.back().y) >= 1e-5) {
        sidePts.push_back(pt);
    }
}

void PushTerminalCap(std::vector<Point2D>& sidePts, Point2D pt, double tX, double tY) {
    while (sidePts.size() > 1) {
        double dx = pt.x - sidePts.back().x;
        double dy = pt.y - sidePts.back().y;
        double forward = dx * tX + dy * tY;
        if (forward < -1e-5) {
            sidePts.pop_back(); // Pop the overshooting interior corner
        } else {
            break;
        }
    }
    sidePts.push_back(pt);
}

void ProcessCorner(const StrokePoint& pt,
                   CapType capType,
                   const SegmentInfo& sIn,
                   const SegmentInfo& sOut,
                   std::vector<Point2D>& leftPts,
                   std::vector<Point2D>& rightPts) {
    double r = std::max(0.0f, pt.r);

    // Compute local average stroke tangent
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

    if (turnAngle < 0.40 && capType == CapType::Round) {
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
        PushMonotonic(leftPts,  { pt.x + offL.x, pt.y + offL.y });
        PushMonotonic(rightPts, { pt.x + offR.x, pt.y + offR.y });
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
                PushMonotonic(outPts, { pt.x + inOff.x, pt.y + inOff.y });
                return;
            }

            if (isInner) {
                // Inner corner: single midpoint prevents retrograde crossing
                PushMonotonic(outPts, { pt.x + (inOff.x + outOff.x) * 0.5, pt.y + (inOff.y + outOff.y) * 0.5 });
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

                PushMonotonic(outPts, { pt.x + inOff.x, pt.y + inOff.y });
                PushMonotonic(outPts, { pt.x + verts[chosenMid].x, pt.y + verts[chosenMid].y });
                PushMonotonic(outPts, { pt.x + outOff.x, pt.y + outOff.y });
            } else {
                PushMonotonic(outPts, { pt.x + inOff.x, pt.y + inOff.y });
                PushMonotonic(outPts, { pt.x + outOff.x, pt.y + outOff.y });
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
    // Precise segment-clamped bisector miter join with disk circumference fallback
    auto emitInner = [&](std::vector<Point2D>& inPts) {
        double sumInX = offIn_inner.x + offOut_inner.x;
        double sumInY = offIn_inner.y + offOut_inner.y;
        double bInLen = std::hypot(sumInX, sumInY);

        if (bInLen < 1e-6) {
            // Hairpin turnaround: inner corner is the pivot point at pt
            PushMonotonic(inPts, { pt.x, pt.y });
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

        innerDist = std::min(innerDist, std::max(0.0, safeLimit));
        Point2D miterPt = { pt.x + bInX * innerDist, pt.y + bInY * innerDist };
        PushMonotonic(inPts, miterPt);
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
            Point2D arcPt = { pt.x + std::cos(a) * r, pt.y + std::sin(a) * r };
            PushMonotonic(outPts, arcPt);
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

} // namespace InkInternal
} // namespace Folio
