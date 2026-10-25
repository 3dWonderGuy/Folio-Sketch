#include "ink_ribbon_corner.hpp"
#include "ink_generator_internal.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {
namespace InkInternal {

/**
 * @brief Appends pt to sidePts if distance from previous vertex exceeds sub-millimeter threshold.
 *
 * Prevents identical duplicate vertices along ribbon rails that could cause zero-length segment
 * division-by-zero or tessellator singularities.
 */
void PushMonotonic(std::vector<Point2D>& sidePts, Point2D pt) {
    if (sidePts.empty()) {
        sidePts.push_back(pt);
        return;
    }
    if (std::hypot(pt.x - sidePts.back().x, pt.y - sidePts.back().y) >= 1e-5) {
        sidePts.push_back(pt);
    }
}

/**
 * @brief Cleans up overshooting rail vertices prior to attaching a terminal cap.
 *
 * When a stroke ends right after a sharp turn, the preceding rail vertices might extend forward
 * beyond the endpoint in the direction of the final tangent (tX, tY). This helper walks backwards
 * and pops vertices until the rail strictly terminates behind the endpoint chord.
 */
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

/**
 * @brief Evaluates ribbon geometry at interior stroke corner Pi joining segment sIn to segment sOut.
 *
 * Geometric Derivation & Turn Classification:
 * -------------------------------------------
 * Let u_in = (ux_in, uy_in) and u_out = (ux_out, uy_out) be the unit tangent vectors.
 * - Dot product:   dot   = u_in . u_out = cos(theta)
 * - 2D Cross prod: cross = u_in x u_out = ux_in * uy_out - uy_in * ux_out = sin(theta)
 * - Turn angle:    theta = atan2(|cross|, dot) in [0, pi]
 *
 * In screen coordinates (+X right, +Y down):
 * - cross > 0: Clockwise turn (RIGHT turn) -> Right rail is inner corner, Left rail is outer corner.
 * - cross < 0: Counter-clockwise turn (LEFT turn) -> Left rail is inner corner, Right rail is outer corner.
 *
 * @param pt Centerline vertex with stroke radius r.
 * @param capType Nib profile (Round, Chisel, Square).
 * @param sIn Incoming segment geometry (tangent, normal, length).
 * @param sOut Outgoing segment geometry (tangent, normal, length).
 * @param leftPts Target container for left boundary vertices.
 * @param rightPts Target container for right boundary vertices.
 */
void ProcessCorner(const StrokePoint& pt,
                   CapType capType,
                   const SegmentInfo& sIn,
                   const SegmentInfo& sOut,
                   std::vector<Point2D>& leftPts,
                   std::vector<Point2D>& rightPts) {
    double r = std::max(0.0f, pt.r);

    // Compute local average stroke tangent (bisector tangent)
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

    // Compute rail support offsets for incoming and outgoing segment normals
    Point2D offL_in, offR_in, offL_out, offR_out;
    GetRibbonOffsets(capType, r, sIn.nx, sIn.ny, offL_in, offR_in);
    GetRibbonOffsets(capType, r, sOut.nx, sOut.ny, offL_out, offR_out);

    double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
    double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
    double turnAngle = std::atan2(std::abs(cross), dot);

    // -------------------------------------------------------------------------
    // Case 1: Gentle Turn (turnAngle < 0.40 rad ≈ 22.9°) with Round Nib
    // -------------------------------------------------------------------------
    // For small angular deviations, evaluating miter bisectors or multi-step arcs
    // produces redundant vertices without visual benefit. We blend into a single
    // vertex per rail along the normalized average normal.
    if (turnAngle < 0.40 && capType == CapType::Round) {
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

    // Identify which rail forms the acute inner corner
    bool leftIsInner = (cross < 0.0) || (cross == 0.0 && dot < 0.0);

    // -------------------------------------------------------------------------
    // Case 2: Polygonal Nibs (Square highlighter / Chisel calligraphy)
    // -------------------------------------------------------------------------
    if (capType == CapType::Square || capType == CapType::Chisel) {
        auto verts = GetNibVertices(capType, r);
        const int K = static_cast<int>(verts.size());

        // Finds the closest nib polygon vertex to an offset vector
        auto findIdx = [&](const Point2D& off) -> int {
            int best = 0;
            double minD = 1e9;
            for (int k = 0; k < K; ++k) {
                double d = std::hypot(verts[k].x - off.x, verts[k].y - off.y);
                if (d < minD) { minD = d; best = k; }
            }
            return best;
        };

        // Elbow bisector vector pointing outward from the turn
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
                // Inner corner: single midpoint prevents retrograde crossing and self-loops
                PushMonotonic(outPts, { pt.x + (inOff.x + outOff.x) * 0.5, pt.y + (inOff.y + outOff.y) * 0.5 });
                return;
            }

            // Outer corner traversal along nib perimeter
            int diff = (idxOut - idxIn + K) % K;
            if (diff == 2) {
                // Opposite diagonal corners: select the intermediate corner facing outward along uElbow
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

    // -------------------------------------------------------------------------
    // Case 3: Circular Nib (Round) - Inner Corner Miter & Outer Bevel Arc
    // -------------------------------------------------------------------------

    /**
     * Inner Corner Evaluation:
     * ------------------------
     * The ideal miter vertex lies along the angle bisector vector:
     *   b_in = normalize(offIn_inner + offOut_inner)
     *
     * In an unconstrained miter, the radial distance from Pi to the miter point is:
     *   d_miter = r / cos(theta / 2)
     * Using the half-angle identity:
     *   cos(theta / 2) = sqrt((1 + dot) / 2)
     *   sin(theta / 2) = sqrt((1 - dot) / 2)
     *
     * As turnAngle approaches pi (acute hairpin turnaround), cos(theta / 2) -> 0,
     * causing d_miter -> infinity (miter explosion defect).
     *
     * To prevent this:
     * 1. Miter Limit: We cap d_miter <= 1.4 * r (standard CAD bevel miter limit).
     * 2. Segment Geometry Limit: The inner corner vertex must NEVER extend backwards
     *    past the midpoint of either adjacent segment:
     *      safeLimit = (min(L_in, L_out) / 2) / sin(theta / 2)
     *    This ensures the inner rail remains strictly bounded inside the segment polygon.
     */
    auto emitInner = [&](std::vector<Point2D>& inPts) {
        double sumInX = offIn_inner.x + offOut_inner.x;
        double sumInY = offIn_inner.y + offOut_inner.y;
        double bInLen = std::hypot(sumInX, sumInY);

        if (bInLen < 1e-6) {
            // Degenerate 180° hairpin turnaround: collapse inner corner to the vertex pivot itself
            PushMonotonic(inPts, { pt.x, pt.y });
            return;
        }

        // Unit bisector vector pointing into the corner
        double bInX = sumInX / bInLen;
        double bInY = sumInY / bInLen;

        // Half-angle trigonometric values derived directly from dot product cos(theta)
        double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
        double sinHalf = std::sqrt(std::max(0.0, (1.0 - dot) * 0.5));

        // Geometric safe limit preventing miter from overreaching neighbor segment midpoints
        double safeLimit = (std::min(sIn.len, sOut.len) * 0.5) / std::max(1e-4, sinHalf);

        double innerDist = r;
        if (dot < -0.70 || bInLen < 0.1) {
            // Acute hairpin turnaround: clamp inner miter distance to pivot cleanly on centerline
            innerDist = 0.0;
        } else if (cosHalf > 1e-4) {
            // Unconstrained miter capped at 1.4 * r
            innerDist = std::min(r / cosHalf, 1.4 * r);
        }

        // Clamp against segment safe limit
        innerDist = std::min(innerDist, std::max(0.0, safeLimit));
        Point2D miterPt = { pt.x + bInX * innerDist, pt.y + bInY * innerDist };
        PushMonotonic(inPts, miterPt);
    };

    /**
     * Outer Corner Evaluation:
     * ------------------------
     * The outer boundary wraps smoothly around the corner using an adaptive circular arc
     * centered at Pi with radius r.
     * The arc starts at offIn_outer, sweeps by turnAngle, and ends at offOut_outer.
     * Tessellation density is adaptively allocated (2 to 24 steps) proportional to turn angle:
     *   steps = clamp(ceil(turnAngle / (pi / 16)), 2, 24)
     */
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

    // Emit inner and outer vertices to their respective rails
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

