/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_corner_pivot.cpp
 * @brief Shared Inner Corner Pivot Pre-Pass for Multi-Sample L-Joints and V-Pockets
 * =========================================================================================
 */

#include "mesh/ink_mesh_internal.hpp"

namespace Folio {
namespace InkInternal {

void ResolveRoundCornerPivots(const std::vector<StrokePoint>& cleanPts,
                              const std::vector<SegmentInfo>& segs,
                              size_t startIdx,
                              size_t endIdx,
                              double gentleAngleLimit,
                              InkMeshAccumulator& acc,
                              std::vector<PinnedCornerSide>& pinnedLeft,
                              std::vector<PinnedCornerSide>& pinnedRight) {
    const float delta = acc.delta;

    auto evalCornerLines = [&](size_t m0, size_t m1, double innerSign,
                               double& outApexX, double& outApexY,
                               double& outTIn0, double& outTOut0,
                               double& outTanHalf, double& outCosHalf,
                               double& outBx, double& outBy) -> bool {
        const auto& sEnter = segs[m0 - 1];
        const auto& sLeave = segs[m1];
        double crossUL = sEnter.ux * sLeave.uy - sEnter.uy * sLeave.ux;
        double dotUL   = sEnter.ux * sLeave.ux + sEnter.uy * sLeave.uy;
        if (innerSign * crossUL >= -1e-3 || dotUL <= -0.985) return false;

        double dx = cleanPts[m1].x - cleanPts[m0].x;
        double dy = cleanPts[m1].y - cleanPts[m0].y;
        outTIn0  = (dx * sLeave.uy - dy * sLeave.ux) / crossUL;
        outTOut0 = (dx * sEnter.uy - dy * sEnter.ux) / crossUL;

        // Ensure ray intersection lies forward along sEnter from cleanPts[m0 - 1]
        // and backward along sLeave from cleanPts[m1 + 1] (rejects converging loop crossovers).
        if (outTIn0 < -0.25 * sEnter.len || outTOut0 > 0.25 * sLeave.len) {
            return false;
        }

        outApexX = cleanPts[m0].x + outTIn0 * sEnter.ux;
        outApexY = cleanPts[m0].y + outTIn0 * sEnter.uy;

        outCosHalf = std::sqrt(std::max(0.0, (1.0 + dotUL) * 0.5));
        double sinHalf = std::sqrt(std::max(0.0, (1.0 - dotUL) * 0.5));
        if (outCosHalf < 0.08) return false;
        outTanHalf = sinHalf / outCosHalf;

        double bx = sEnter.nx + sLeave.nx;
        double by = sEnter.ny + sLeave.ny;
        double bLen = std::hypot(bx, by);
        if (bLen <= 1e-6) return false;
        outBx = bx / bLen;
        outBy = by / bLen;
        return true;
    };

    for (size_t k = startIdx + 1; k < endIdx; ++k) {
        const auto& sIn  = segs[k - 1];
        const auto& sOut = segs[k];
        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
        double turnAngle = std::atan2(std::abs(cross), dot);
        if (turnAngle < gentleAngleLimit) continue;

        bool leftIsInner = (cross < 0.0) || (cross == 0.0 && dot < 0.0);
        double innerSign = leftIsInner ? 1.0 : -1.0;
        auto& targetPins = leftIsInner ? pinnedLeft : pinnedRight;
        if (targetPins[k - startIdx].nomIdx != UINT32_MAX ||
            targetPins[k - 1 - startIdx].nomIdx != UINT32_MAX) {
            continue;
        }

        // 1. Group contiguous vertices that turn sharply in the same direction around the corner,
        //    stopping before total turn exceeds ~166 deg so curved loops don't wind past 180 deg.
        size_t c0 = k;
        size_t c1 = k;
        double cumTurn = turnAngle;
        while (c1 + 1 < endIdx &&
               targetPins[c1 + 1 - startIdx].nomIdx == UINT32_MAX) {
            const auto& sA = segs[c1];
            const auto& sB = segs[c1 + 1];
            double cNext = sA.ux * sB.uy - sA.uy * sB.ux;
            double dNext = sA.ux * sB.ux + sA.uy * sB.uy;
            double aNext = std::atan2(std::abs(cNext), dNext);
            if (innerSign * cNext < -1e-3 && aNext >= gentleAngleLimit && (cumTurn + aNext) <= 2.90) {
                ++c1;
                cumTurn += aNext;
            } else {
                break;
            }
        }

        size_t m0 = c0;
        size_t m1 = c1;
        double apexX = cleanPts[k].x, apexY = cleanPts[k].y;
        double tIn0 = 0.0, tOut0 = 0.0, tanHalf = 0.0, cosHalf = 1.0, bx = sIn.nx, by = sIn.ny;
        if (!evalCornerLines(m0, m1, innerSign, apexX, apexY, tIn0, tOut0, tanHalf, cosHalf, bx, by)) {
            m0 = k;
            m1 = k;
            c0 = k;
            c1 = k;
            if (!evalCornerLines(m0, m1, innerSign, apexX, apexY, tIn0, tOut0, tanHalf, cosHalf, bx, by)) {
                continue;
            }
        }

        const auto& sBaseEnter = segs[c0 - 1];
        const auto& sBaseLeave = segs[c1];

        // 2. Expand [m0, m1] symmetrically backward and forward along straight incoming/outgoing arms
        //    (or outward-separating U-turn arms where cTanHalf < tanHalf).
        //    Do NOT expand along inward-curving cursive loop arms where the arm rotates away from sBaseEnter/sBaseLeave
        //    and causes cTanHalf to grow.
        auto tryExpandBackward = [&](double reqTrim, double spanIn) -> bool {
            if (spanIn >= reqTrim || m0 <= startIdx + 1 ||
                targetPins[m0 - 1 - startIdx].nomIdx != UINT32_MAX) {
                return false;
            }
            const auto& sPrev = segs[m0 - 2];
            const auto& sCur  = segs[m0 - 1];
            double cStep = sPrev.ux * sCur.uy - sPrev.uy * sCur.ux;
            double cApexX, cApexY, cTIn0, cTOut0, cTanHalf, cCosHalf, cBx, cBy;
            if (!evalCornerLines(m0 - 1, m1, innerSign, cApexX, cApexY, cTIn0, cTOut0, cTanHalf, cCosHalf, cBx, cBy)) {
                return false;
            }
            double dotFromBase = sPrev.ux * sBaseEnter.ux + sPrev.uy * sBaseEnter.uy;
            bool isStraightArm = (std::abs(cStep) <= 0.12 && dotFromBase >= 0.98 && cTanHalf <= tanHalf * 1.15);
            bool isOutwardSep  = (cTanHalf < tanHalf);
            if (isStraightArm || isOutwardSep) {
                --m0;
                apexX = cApexX; apexY = cApexY;
                tIn0 = cTIn0; tOut0 = cTOut0;
                tanHalf = cTanHalf; cosHalf = cCosHalf;
                bx = cBx; by = cBy;
                return true;
            }
            return false;
        };

        auto tryExpandForward = [&](double reqTrim, double spanOut) -> bool {
            if (spanOut >= reqTrim || m1 + 1 >= endIdx ||
                targetPins[m1 + 1 - startIdx].nomIdx != UINT32_MAX) {
                return false;
            }
            const auto& sCur  = segs[m1];
            const auto& sNext = segs[m1 + 1];
            double cStep = sCur.ux * sNext.uy - sCur.uy * sNext.ux;
            double cApexX, cApexY, cTIn0, cTOut0, cTanHalf, cCosHalf, cBx, cBy;
            if (!evalCornerLines(m0, m1 + 1, innerSign, cApexX, cApexY, cTIn0, cTOut0, cTanHalf, cCosHalf, cBx, cBy)) {
                return false;
            }
            double dotFromBase = sNext.ux * sBaseLeave.ux + sNext.uy * sBaseLeave.uy;
            bool isStraightArm = (std::abs(cStep) <= 0.12 && dotFromBase >= 0.98 && cTanHalf <= tanHalf * 1.15);
            bool isOutwardSep  = (cTanHalf < tanHalf);
            if (isStraightArm || isOutwardSep) {
                ++m1;
                apexX = cApexX; apexY = cApexY;
                tIn0 = cTIn0; tOut0 = cTOut0;
                tanHalf = cTanHalf; cosHalf = cCosHalf;
                bx = cBx; by = cBy;
                return true;
            }
            return false;
        };

        bool expanded = true;
        while (expanded) {
            expanded = false;
            float rRef = std::max(0.0f, 0.5f * (cleanPts[m0].r + cleanPts[m1].r));
            double reqTrim = static_cast<double>(rRef + delta) * tanHalf;
            double spanIn  = tIn0 + segs[m0 - 1].len;
            double spanOut = segs[m1].len - tOut0;

            if (spanIn <= spanOut) {
                expanded = tryExpandBackward(reqTrim, spanIn);
                if (!expanded) expanded = tryExpandForward(reqTrim, spanOut);
            } else {
                expanded = tryExpandForward(reqTrim, spanOut);
                if (!expanded) expanded = tryExpandBackward(reqTrim, spanIn);
            }
        }

        float rCorner = std::max(0.0f, 0.5f * (cleanPts[m0].r + cleanPts[m1].r));
        float skirtDistCorner = 1.0f + delta / std::max(1e-4f, rCorner);

        // Bound pivot distance strictly by the actual pinned span [m0 - 1, m1 + 1]
        // so pivCore/pivSkirt never jump past unpinned vertices and never depend on future stroke length.
        double spanInFinal  = tIn0 + segs[m0 - 1].len;
        double spanOutFinal = segs[m1].len - tOut0;
        double dMaxIn   = spanInFinal  / std::max(1e-4, tanHalf);
        double dMaxOut  = spanOutFinal / std::max(1e-4, tanHalf);
        double dMaxPerp = std::max(0.0, std::min(dMaxIn, dMaxOut));

        double dCorePerp  = static_cast<double>(rCorner);
        double dSkirtPerp = static_cast<double>(rCorner + delta);
        if (dSkirtPerp > dMaxPerp && dSkirtPerp > 1e-6) {
            double scale = dMaxPerp / dSkirtPerp;
            dCorePerp  *= scale;
            dSkirtPerp  = dMaxPerp;
        }

        double coreBisectDist  = dCorePerp / std::max(0.08, cosHalf);
        double skirtBisectDist = dSkirtPerp / std::max(0.08, cosHalf);
        double pivCoreX  = apexX + innerSign * bx * coreBisectDist;
        double pivCoreY  = apexY + innerSign * by * coreBisectDist;
        double pivSkirtX = apexX + innerSign * bx * skirtBisectDist;
        double pivSkirtY = apexY + innerSign * by * skirtBisectDist;

        // Trim [m0, m1] so vertices that lie before pivCore on the incoming arm or after pivCore
        // on the outgoing arm (e.g. when dCorePerp is clamped by a shorter opposite arm) are not over-pinned.
        while (m0 < c0) {
            const auto& sPrev = segs[m0 - 1];
            double stepToPiv = (pivCoreX - cleanPts[m0].x) * sPrev.ux +
                               (pivCoreY - cleanPts[m0].y) * sPrev.uy;
            if (stepToPiv > 1e-3) {
                ++m0;
            } else {
                break;
            }
        }
        while (m1 > c1) {
            const auto& sNext = segs[m1];
            double stepFromPiv = (cleanPts[m1].x - pivCoreX) * sNext.ux +
                                 (cleanPts[m1].y - pivCoreY) * sNext.uy;
            if (stepFromPiv > 1e-3) {
                --m1;
            } else {
                break;
            }
        }

        // Verify pivot lies strictly on the inner side of ALL connected segments [m0 - 1, m1],
        // and has sufficient bisector reach (coreBisectDist >= 0.40 * rCorner).
        bool validFan = (coreBisectDist >= 0.40 * static_cast<double>(rCorner) && dSkirtPerp > dCorePerp + 1e-4);
        for (size_t m = m0 - 1; m <= m1 && validFan; ++m) {
            const auto& sm = segs[m];
            double proj0 = (pivCoreX - cleanPts[m].x)     * (innerSign * sm.nx) +
                           (pivCoreY - cleanPts[m].y)     * (innerSign * sm.ny);
            double proj1 = (pivCoreX - cleanPts[m + 1].x) * (innerSign * sm.nx) +
                           (pivCoreY - cleanPts[m + 1].y) * (innerSign * sm.ny);
            if (proj0 <= 1e-4 || proj1 <= 1e-4) validFan = false;
        }
        if (!validFan) continue;

        uint32_t sharedNomIdx   = acc.addVertex(pivCoreX, pivCoreY, 1.0f, rCorner);
        uint32_t sharedSkirtIdx = acc.addVertex(pivSkirtX, pivSkirtY, skirtDistCorner, rCorner);
        for (size_t m = m0; m <= m1; ++m) {
            targetPins[m - startIdx] = { sharedNomIdx, sharedSkirtIdx };
        }
    }
}

} // namespace InkInternal
} // namespace Folio
