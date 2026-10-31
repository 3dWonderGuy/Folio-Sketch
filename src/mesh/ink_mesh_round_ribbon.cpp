/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_round_ribbon.cpp
 * @brief Round Sub-Ribbon Mesh Builder (Continuous Miters, Inner Pivots, and Outer Bevel Fans)
 * =========================================================================================
 */

#include "mesh/ink_mesh_internal.hpp"

namespace Folio {
namespace InkInternal {

void BuildRoundSubRibbon(const std::vector<StrokePoint>& cleanPts,
                         const std::vector<SegmentInfo>& segs,
                         size_t startIdx,
                         size_t endIdx,
                         double gentleAngleLimit,
                         double miterMaxClamp,
                         InkMeshAccumulator& acc) {
    if (endIdx <= startIdx) return;
    const size_t subN = endIdx - startIdx + 1;
    if (subN < 2) return;

    const float delta = acc.delta;
    float rStart = std::max(0.0f, cleanPts[startIdx].r);
    const auto& s0 = segs[startIdx];
    float skirtDistStart = 1.0f + delta / std::max(1e-4f, rStart);

    uint32_t curLeftIdx   = acc.addVertex(cleanPts[startIdx].x + s0.nx * rStart,
                                          cleanPts[startIdx].y + s0.ny * rStart, 1.0f, rStart);
    uint32_t curCenterIdx = acc.addVertex(cleanPts[startIdx].x, cleanPts[startIdx].y, 0.0f, rStart);
    uint32_t curRightIdx  = acc.addVertex(cleanPts[startIdx].x - s0.nx * rStart,
                                          cleanPts[startIdx].y - s0.ny * rStart, 1.0f, rStart);

    uint32_t curLeftSkirtIdx  = acc.addVertex(cleanPts[startIdx].x + s0.nx * (rStart + delta),
                                              cleanPts[startIdx].y + s0.ny * (rStart + delta), skirtDistStart, rStart);
    uint32_t curRightSkirtIdx = acc.addVertex(cleanPts[startIdx].x - s0.nx * (rStart + delta),
                                              cleanPts[startIdx].y - s0.ny * (rStart + delta), skirtDistStart, rStart);

    // 1. Head Endcap at startIdx
    EmitRoundHeadCap(cleanPts[startIdx], s0, rStart, skirtDistStart,
                     curCenterIdx, curLeftIdx, curLeftSkirtIdx, curRightIdx, curRightSkirtIdx, acc);

    // 2. Pre-pass: Resolve shared inner corner pivots
    std::vector<PinnedCornerSide> pinnedLeft(subN);
    std::vector<PinnedCornerSide> pinnedRight(subN);
    ResolveRoundCornerPivots(cleanPts, segs, startIdx, endIdx, gentleAngleLimit, acc, pinnedLeft, pinnedRight);

    // 3. Traverse interior and terminal segments
    for (size_t i = startIdx; i < endIdx; ++i) {
        const size_t nextPtIdx = i + 1;
        float rNext = std::max(0.0f, cleanPts[nextPtIdx].r);
        float skirtDistNext = 1.0f + delta / std::max(1e-4f, rNext);

        if (nextPtIdx == endIdx) {
            // Terminal segment ending at endIdx
            const auto& lastSeg = segs[i];
            uint32_t nextCenterIdx = acc.addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);

            double candLX  = cleanPts[nextPtIdx].x + lastSeg.nx * rNext;
            double candLY  = cleanPts[nextPtIdx].y + lastSeg.ny * rNext;
            double candLSX = cleanPts[nextPtIdx].x + lastSeg.nx * (rNext + delta);
            double candLSY = cleanPts[nextPtIdx].y + lastSeg.ny * (rNext + delta);
            double stepLU  = (candLX  - acc.outMesh.vertices[curLeftIdx].x)      * lastSeg.ux + (candLY  - acc.outMesh.vertices[curLeftIdx].y)      * lastSeg.uy;
            double stepLSU = (candLSX - acc.outMesh.vertices[curLeftSkirtIdx].x) * lastSeg.ux + (candLSY - acc.outMesh.vertices[curLeftSkirtIdx].y) * lastSeg.uy;

            uint32_t nextLeftIdx      = curLeftIdx;
            uint32_t nextLeftSkirtIdx = curLeftSkirtIdx;
            if (stepLU > 1e-3 && stepLSU > 1e-3) {
                nextLeftIdx      = acc.addVertex(candLX, candLY, 1.0f, rNext);
                nextLeftSkirtIdx = acc.addVertex(candLSX, candLSY, skirtDistNext, rNext);
                acc.addCoreQuad(curLeftIdx, nextLeftIdx, nextCenterIdx, curCenterIdx);
                acc.addSkirtQuad(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx, nextLeftIdx);
            } else {
                acc.addCoreTriangle(curLeftIdx, nextCenterIdx, curCenterIdx);
            }

            double candRX  = cleanPts[nextPtIdx].x - lastSeg.nx * rNext;
            double candRY  = cleanPts[nextPtIdx].y - lastSeg.ny * rNext;
            double candRSX = cleanPts[nextPtIdx].x - lastSeg.nx * (rNext + delta);
            double candRSY = cleanPts[nextPtIdx].y - lastSeg.ny * (rNext + delta);
            double stepRU  = (candRX  - acc.outMesh.vertices[curRightIdx].x)      * lastSeg.ux + (candRY  - acc.outMesh.vertices[curRightIdx].y)      * lastSeg.uy;
            double stepRSU = (candRSX - acc.outMesh.vertices[curRightSkirtIdx].x) * lastSeg.ux + (candRSY - acc.outMesh.vertices[curRightSkirtIdx].y) * lastSeg.uy;

            uint32_t nextRightIdx      = curRightIdx;
            uint32_t nextRightSkirtIdx = curRightSkirtIdx;
            if (stepRU > 1e-3 && stepRSU > 1e-3) {
                nextRightIdx      = acc.addVertex(candRX, candRY, 1.0f, rNext);
                nextRightSkirtIdx = acc.addVertex(candRSX, candRSY, skirtDistNext, rNext);
                acc.addCoreQuad(curCenterIdx, nextCenterIdx, nextRightIdx, curRightIdx);
                acc.addSkirtQuad(curRightIdx, nextRightIdx, nextRightSkirtIdx, curRightSkirtIdx);
            } else {
                acc.addCoreTriangle(curCenterIdx, nextCenterIdx, curRightIdx);
            }

            // Tail Endcap at endIdx
            EmitRoundTailCap(cleanPts[nextPtIdx], lastSeg, rNext, skirtDistNext,
                             nextCenterIdx, nextLeftIdx, nextLeftSkirtIdx, nextRightIdx, nextRightSkirtIdx, acc);
            break;
        }

        // Interior vertex P_{i+1}
        const auto& sIn  = segs[i];
        const auto& sOut = segs[i + 1];

        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
        double turnAngle = std::atan2(std::abs(cross), dot);

        double avgNx = sIn.nx + sOut.nx;
        double avgNy = sIn.ny + sOut.ny;
        double avgLen = std::hypot(avgNx, avgNy);
        if (avgLen > 1e-6) { avgNx /= avgLen; avgNy /= avgLen; }
        else { avgNx = sIn.nx; avgNy = sIn.ny; }

        double sinHalf = std::sqrt(std::max(0.0, (1.0 - dot) * 0.5));
        double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
        double safeLimit = (std::min(sIn.len, sOut.len) * 0.45) / std::max(1e-4, sinHalf);

        double miterScale = 1.0 / std::max(0.5, cosHalf);
        miterScale = std::min(miterScale, miterMaxClamp);
        double nomMiterDist = rNext * miterScale;
        double nomSkirtDist = nomMiterDist + delta;

        const auto& pinL = pinnedLeft[nextPtIdx - startIdx];
        const auto& pinR = pinnedRight[nextPtIdx - startIdx];
        const bool isPinnedL = (pinL.nomIdx != UINT32_MAX);
        const bool isPinnedR = (pinR.nomIdx != UINT32_MAX);

        const bool sharpTurn = (turnAngle >= gentleAngleLimit || nomSkirtDist > safeLimit);
        const bool needRightOuterFan = sharpTurn && (cross < -1e-4) && !isPinnedR;
        const bool needLeftOuterFan  = sharpTurn && (cross >  1e-4) && !isPinnedL;

        if (!needRightOuterFan && !needLeftOuterFan) {
            uint32_t nextCenterIdx = acc.addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);

            double leftCoreDist  = nomMiterDist;
            double leftSkirtDist = nomSkirtDist;
            if (cross < 0.0 && leftSkirtDist > safeLimit && leftSkirtDist > 1e-6) {
                double scale = safeLimit / leftSkirtDist;
                leftCoreDist *= scale;
                leftSkirtDist = safeLimit;
            }
            double rightCoreDist  = nomMiterDist;
            double rightSkirtDist = nomSkirtDist;
            if (cross > 0.0 && rightSkirtDist > safeLimit && rightSkirtDist > 1e-6) {
                double scale = safeLimit / rightSkirtDist;
                rightCoreDist *= scale;
                rightSkirtDist = safeLimit;
            }

            uint32_t nextLeftIdx      = curLeftIdx;
            uint32_t nextLeftSkirtIdx = curLeftSkirtIdx;
            if (isPinnedL) {
                nextLeftIdx      = pinL.nomIdx;
                nextLeftSkirtIdx = pinL.skirtIdx;
                if (curLeftIdx == nextLeftIdx) {
                    acc.addCoreTriangle(curLeftIdx, nextCenterIdx, curCenterIdx);
                } else {
                    acc.addCoreQuad(curLeftIdx, nextLeftIdx, nextCenterIdx, curCenterIdx);
                    acc.addSkirtQuad(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx, nextLeftIdx);
                }
            } else {
                double candLX  = cleanPts[nextPtIdx].x + avgNx * leftCoreDist;
                double candLY  = cleanPts[nextPtIdx].y + avgNy * leftCoreDist;
                double candLSX = cleanPts[nextPtIdx].x + avgNx * leftSkirtDist;
                double candLSY = cleanPts[nextPtIdx].y + avgNy * leftSkirtDist;
                double stepLU  = (candLX  - acc.outMesh.vertices[curLeftIdx].x)      * sIn.ux + (candLY  - acc.outMesh.vertices[curLeftIdx].y)      * sIn.uy;
                double stepLSU = (candLSX - acc.outMesh.vertices[curLeftSkirtIdx].x) * sIn.ux + (candLSY - acc.outMesh.vertices[curLeftSkirtIdx].y) * sIn.uy;
                if (stepLU > 1e-3 && stepLSU > 1e-3) {
                    nextLeftIdx      = acc.addVertex(candLX, candLY, 1.0f, rNext);
                    nextLeftSkirtIdx = acc.addVertex(candLSX, candLSY, skirtDistNext, rNext);
                    acc.addCoreQuad(curLeftIdx, nextLeftIdx, nextCenterIdx, curCenterIdx);
                    acc.addSkirtQuad(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx, nextLeftIdx);
                } else {
                    acc.addCoreTriangle(curLeftIdx, nextCenterIdx, curCenterIdx);
                }
            }

            uint32_t nextRightIdx      = curRightIdx;
            uint32_t nextRightSkirtIdx = curRightSkirtIdx;
            if (isPinnedR) {
                nextRightIdx      = pinR.nomIdx;
                nextRightSkirtIdx = pinR.skirtIdx;
                if (curRightIdx == nextRightIdx) {
                    acc.addCoreTriangle(curCenterIdx, nextCenterIdx, curRightIdx);
                } else {
                    acc.addCoreQuad(curCenterIdx, nextCenterIdx, nextRightIdx, curRightIdx);
                    acc.addSkirtQuad(curRightIdx, nextRightIdx, nextRightSkirtIdx, curRightSkirtIdx);
                }
            } else {
                double candRX  = cleanPts[nextPtIdx].x - avgNx * rightCoreDist;
                double candRY  = cleanPts[nextPtIdx].y - avgNy * rightCoreDist;
                double candRSX = cleanPts[nextPtIdx].x - avgNx * rightSkirtDist;
                double candRSY = cleanPts[nextPtIdx].y - avgNy * rightSkirtDist;
                double stepRU  = (candRX  - acc.outMesh.vertices[curRightIdx].x)      * sIn.ux + (candRY  - acc.outMesh.vertices[curRightIdx].y)      * sIn.uy;
                double stepRSU = (candRSX - acc.outMesh.vertices[curRightSkirtIdx].x) * sIn.ux + (candRSY - acc.outMesh.vertices[curRightSkirtIdx].y) * sIn.uy;
                if (stepRU > 1e-3 && stepRSU > 1e-3) {
                    nextRightIdx      = acc.addVertex(candRX, candRY, 1.0f, rNext);
                    nextRightSkirtIdx = acc.addVertex(candRSX, candRSY, skirtDistNext, rNext);
                    acc.addCoreQuad(curCenterIdx, nextCenterIdx, nextRightIdx, curRightIdx);
                    acc.addSkirtQuad(curRightIdx, nextRightIdx, nextRightSkirtIdx, curRightSkirtIdx);
                } else {
                    acc.addCoreTriangle(curCenterIdx, nextCenterIdx, curRightIdx);
                }
            }

            curLeftIdx        = nextLeftIdx;
            curLeftSkirtIdx   = nextLeftSkirtIdx;
            curCenterIdx      = nextCenterIdx;
            curRightIdx       = nextRightIdx;
            curRightSkirtIdx  = nextRightSkirtIdx;
            continue;
        }

        // Sharp turn with an outer bevel fan on either the right side (leftIsInner=true) or left side (leftIsInner=false)
        bool leftIsInner = needRightOuterFan;
        uint32_t nextCenterIdx = acc.addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);

        uint32_t innerIdx = UINT32_MAX;
        uint32_t innerSkirtIdx = UINT32_MAX;
        if (leftIsInner && isPinnedL) {
            innerIdx      = pinL.nomIdx;
            innerSkirtIdx = pinL.skirtIdx;
        } else if (!leftIsInner && isPinnedR) {
            innerIdx      = pinR.nomIdx;
            innerSkirtIdx = pinR.skirtIdx;
        } else {
            double innerSign = leftIsInner ? 1.0 : -1.0;
            double innerMiterDist = nomMiterDist;
            double innerSkirtDist = nomSkirtDist;
            if (innerSkirtDist > safeLimit && innerSkirtDist > 1e-6) {
                double scale = safeLimit / innerSkirtDist;
                innerMiterDist *= scale;
                innerSkirtDist  = safeLimit;
            }
            uint32_t prevInnerNom   = leftIsInner ? curLeftIdx : curRightIdx;
            uint32_t prevInnerSkirt = leftIsInner ? curLeftSkirtIdx : curRightSkirtIdx;
            double candCoreX  = cleanPts[nextPtIdx].x + innerSign * avgNx * innerMiterDist;
            double candCoreY  = cleanPts[nextPtIdx].y + innerSign * avgNy * innerMiterDist;
            double candSkirtX = cleanPts[nextPtIdx].x + innerSign * avgNx * innerSkirtDist;
            double candSkirtY = cleanPts[nextPtIdx].y + innerSign * avgNy * innerSkirtDist;

            const auto& vPrevNom   = acc.outMesh.vertices[prevInnerNom];
            const auto& vPrevSkirt = acc.outMesh.vertices[prevInnerSkirt];
            double stepCoreU  = (candCoreX  - vPrevNom.x)   * sIn.ux + (candCoreY  - vPrevNom.y)   * sIn.uy;
            double stepSkirtU = (candSkirtX - vPrevSkirt.x) * sIn.ux + (candSkirtY - vPrevSkirt.y) * sIn.uy;
            if (stepCoreU <= 1e-3 || stepSkirtU <= 1e-3) {
                innerIdx      = prevInnerNom;
                innerSkirtIdx = prevInnerSkirt;
            } else {
                innerIdx      = acc.addVertex(candCoreX, candCoreY, 1.0f, rNext);
                innerSkirtIdx = acc.addVertex(candSkirtX, candSkirtY, skirtDistNext, rNext);
            }
        }

        double outerSign = leftIsInner ? -1.0 : 1.0;
        uint32_t outerInIdx = acc.addVertex(cleanPts[nextPtIdx].x + outerSign * sIn.nx * rNext,
                                            cleanPts[nextPtIdx].y + outerSign * sIn.ny * rNext,
                                            1.0f, rNext);
        uint32_t outerInSkirtIdx = acc.addVertex(cleanPts[nextPtIdx].x + outerSign * sIn.nx * (rNext + delta),
                                                 cleanPts[nextPtIdx].y + outerSign * sIn.ny * (rNext + delta),
                                                 skirtDistNext, rNext);

        if (leftIsInner) {
            if (curLeftIdx == innerIdx) {
                acc.addCoreTriangle(curLeftIdx, nextCenterIdx, curCenterIdx);
            } else {
                acc.addCoreQuad(curLeftIdx, innerIdx, nextCenterIdx, curCenterIdx);
                acc.addSkirtQuad(curLeftIdx, curLeftSkirtIdx, innerSkirtIdx, innerIdx);
            }
            acc.addCoreQuad(curCenterIdx, nextCenterIdx, outerInIdx, curRightIdx);
            acc.addSkirtQuad(curRightIdx, outerInIdx, outerInSkirtIdx, curRightSkirtIdx);
        } else {
            acc.addCoreQuad(curLeftIdx, outerInIdx, nextCenterIdx, curCenterIdx);
            acc.addSkirtQuad(curLeftIdx, curLeftSkirtIdx, outerInSkirtIdx, outerInIdx);
            if (curRightIdx == innerIdx) {
                acc.addCoreTriangle(curCenterIdx, nextCenterIdx, curRightIdx);
            } else {
                acc.addCoreQuad(curCenterIdx, nextCenterIdx, innerIdx, curRightIdx);
                acc.addSkirtQuad(curRightIdx, innerIdx, innerSkirtIdx, curRightSkirtIdx);
            }
        }

        uint32_t prevFanNom = outerInIdx;
        uint32_t prevFanSkirt = outerInSkirtIdx;
        bool outerTurnsOutward = leftIsInner ? (cross < -1e-4) : (cross > 1e-4);
        if (outerTurnsOutward && turnAngle >= 1e-3) {
            int fanSteps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 16.0))), 1, 16);
            double aStartFan = std::atan2(outerSign * sIn.ny,  outerSign * sIn.nx);
            double aEndFan   = std::atan2(outerSign * sOut.ny, outerSign * sOut.nx);
            double sweepFan = aEndFan - aStartFan;
            if (leftIsInner) {
                while (sweepFan > 0.0) sweepFan -= 2.0 * M_PI;
                while (sweepFan < -2.0 * M_PI) sweepFan += 2.0 * M_PI;
            } else {
                while (sweepFan < 0.0) sweepFan += 2.0 * M_PI;
                while (sweepFan > 2.0 * M_PI) sweepFan -= 2.0 * M_PI;
            }

            for (int s = 1; s <= fanSteps; ++s) {
                double a = (s == fanSteps) ? aEndFan : (aStartFan + (static_cast<double>(s) / fanSteps) * sweepFan);
                uint32_t arcNomIdx = acc.addVertex(cleanPts[nextPtIdx].x + std::cos(a) * rNext,
                                                   cleanPts[nextPtIdx].y + std::sin(a) * rNext,
                                                   1.0f, rNext);
                uint32_t arcSkirtIdx = acc.addVertex(cleanPts[nextPtIdx].x + std::cos(a) * (rNext + delta),
                                                     cleanPts[nextPtIdx].y + std::sin(a) * (rNext + delta),
                                                     skirtDistNext, rNext);
                if (leftIsInner) {
                    acc.addCoreTriangle(nextCenterIdx, arcNomIdx, prevFanNom);
                    acc.addSkirtTriangle(prevFanNom, arcNomIdx, arcSkirtIdx);
                    acc.addSkirtTriangle(prevFanNom, arcSkirtIdx, prevFanSkirt);
                } else {
                    acc.addCoreTriangle(nextCenterIdx, prevFanNom, arcNomIdx);
                    acc.addSkirtTriangle(prevFanNom, prevFanSkirt, arcSkirtIdx);
                    acc.addSkirtTriangle(prevFanNom, arcSkirtIdx, arcNomIdx);
                }
                prevFanNom = arcNomIdx;
                prevFanSkirt = arcSkirtIdx;
            }
        }

        curCenterIdx = nextCenterIdx;
        if (leftIsInner) {
            curLeftIdx        = innerIdx;
            curLeftSkirtIdx   = innerSkirtIdx;
            curRightIdx       = prevFanNom;
            curRightSkirtIdx  = prevFanSkirt;
        } else {
            curLeftIdx        = prevFanNom;
            curLeftSkirtIdx   = prevFanSkirt;
            curRightIdx       = innerIdx;
            curRightSkirtIdx  = innerSkirtIdx;
        }
    }
}

} // namespace InkInternal
} // namespace Folio
