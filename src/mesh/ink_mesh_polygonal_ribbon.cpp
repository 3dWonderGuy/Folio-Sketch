/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_polygonal_ribbon.cpp
 * @brief Polygonal Nib Sub-Ribbon Builder (Square/Highlighter and Chisel/Calligraphy)
 * =========================================================================================
 */

#include "mesh/ink_mesh_internal.hpp"

namespace Folio {
namespace InkInternal {

void BuildPolygonalSubRibbon(const std::vector<StrokePoint>& cleanPts,
                             const std::vector<SegmentInfo>& segs,
                             size_t startIdx,
                             size_t endIdx,
                             CapType capType,
                             InkMeshAccumulator& acc) {
    if (endIdx <= startIdx) return;
    const size_t subN = endIdx - startIdx + 1;
    if (subN < 2) return;

    const float delta = acc.delta;

    struct SegmentCorners {
        int idxLeftStart  = 0;
        int idxLeftEnd    = 0;
        int idxRightStart = 0;
        int idxRightEnd   = 0;
    };

    const size_t numSubSegs = endIdx - startIdx;
    std::vector<SegmentCorners> segCorners(numSubSegs);

    for (size_t si = 0; si < numSubSegs; ++si) {
        size_t segIdx = startIdx + si;
        const auto& s = segs[segIdx];
        float rAvg = std::max(0.0f, (cleanPts[segIdx].r + cleanPts[segIdx + 1].r) * 0.5f);
        auto verts = GetNibVertices(capType, rAvg);
        const int K = static_cast<int>(verts.size());

        double maxProj = -1e9, minProj = 1e9;
        for (int k = 0; k < K; ++k) {
            double proj = verts[k].x * s.nx + verts[k].y * s.ny;
            if (proj > maxProj) maxProj = proj;
            if (proj < minProj) minProj = proj;
        }

        int lStart = 0, lEnd = 0, rStart = 0, rEnd = 0;
        double lStartTan = 1e9, lEndTan = -1e9;
        double rStartTan = 1e9, rEndTan = -1e9;

        for (int k = 0; k < K; ++k) {
            double proj = verts[k].x * s.nx + verts[k].y * s.ny;
            double tan  = verts[k].x * s.ux + verts[k].y * s.uy;

            if (std::abs(proj - maxProj) <= 1e-4) {
                if (tan < lStartTan) { lStartTan = tan; lStart = k; }
                if (tan > lEndTan)   { lEndTan   = tan; lEnd   = k; }
            }
            if (std::abs(proj - minProj) <= 1e-4) {
                if (tan < rStartTan) { rStartTan = tan; rStart = k; }
                if (tan > rEndTan)   { rEndTan   = tan; rEnd   = k; }
            }
        }

        segCorners[si] = { lStart, lEnd, rStart, rEnd };
    }

    // Helper to emit a polygon fan between two corner indices at point P with exterior skirts
    auto emitPolygonFan = [&](uint32_t cIdx, const StrokePoint& pt, float r,
                              int startCorner, int endCorner,
                              uint32_t startVtxIdx, uint32_t startSkirtIdx,
                              uint32_t endVtxIdx, uint32_t endSkirtIdx,
                              double outwardX, double outwardY, float edgeDist) {
        if (startCorner == endCorner) return;
        auto verts = GetNibVertices(capType, r);
        const int K = static_cast<int>(verts.size());

        std::vector<int> pathCW;
        for (int k = (startCorner + 1) % K; k != endCorner; k = (k + 1) % K) {
            pathCW.push_back(k);
        }
        std::vector<int> pathCCW;
        for (int k = (startCorner - 1 + K) % K; k != endCorner; k = (k - 1 + K) % K) {
            pathCCW.push_back(k);
        }

        auto evalPath = [&](const std::vector<int>& p) -> double {
            if (p.empty()) return 0.0;
            double s = 0.0;
            for (int idx : p) {
                s += verts[idx].x * outwardX + verts[idx].y * outwardY;
            }
            return s / static_cast<double>(p.size());
        };

        const auto& chosen = (evalPath(pathCW) >= evalPath(pathCCW)) ? pathCW : pathCCW;

        uint32_t prevNom = startVtxIdx;
        uint32_t prevSkirt = startSkirtIdx;
        for (int idx : chosen) {
            double len = std::hypot(verts[idx].x, verts[idx].y);
            double s = (len > 1e-4) ? (1.0 + delta / len) : 1.0;
            uint32_t curNom = acc.addVertex(pt.x + verts[idx].x, pt.y + verts[idx].y, edgeDist, r);
            uint32_t curSkirt = acc.addVertex(pt.x + verts[idx].x * s, pt.y + verts[idx].y * s, edgeDist * static_cast<float>(s), r);
            acc.addCoreTriangle(cIdx, prevNom, curNom);
            acc.addSkirtTriangle(prevNom, prevSkirt, curSkirt);
            acc.addSkirtTriangle(prevNom, curSkirt, curNom);
            prevNom = curNom;
            prevSkirt = curSkirt;
        }
        acc.addCoreTriangle(cIdx, prevNom, endVtxIdx);
        acc.addSkirtTriangle(prevNom, prevSkirt, endSkirtIdx);
        acc.addSkirtTriangle(prevNom, endSkirtIdx, endVtxIdx);
    };

    // Head Cap at startIdx: covers rear-facing corners of the fixed nib
    float rStart = std::max(0.0f, cleanPts[startIdx].r);
    auto vertsStart = GetNibVertices(capType, rStart);
    const auto& sc0 = segCorners[0];
    const auto& s0  = segs[startIdx];

    double lenL0 = std::hypot(vertsStart[sc0.idxLeftStart].x, vertsStart[sc0.idxLeftStart].y);
    double sL0 = (lenL0 > 1e-4) ? (1.0 + delta / lenL0) : 1.0;
    double lenR0 = std::hypot(vertsStart[sc0.idxRightStart].x, vertsStart[sc0.idxRightStart].y);
    double sR0 = (lenR0 > 1e-4) ? (1.0 + delta / lenR0) : 1.0;

    uint32_t curLeftIdx   = acc.addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxLeftStart].x,
                                          cleanPts[startIdx].y + vertsStart[sc0.idxLeftStart].y, 1.0f, rStart);
    uint32_t curCenterIdx = acc.addVertex(cleanPts[startIdx].x, cleanPts[startIdx].y, 0.0f, rStart);
    uint32_t curRightIdx  = acc.addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxRightStart].x,
                                          cleanPts[startIdx].y + vertsStart[sc0.idxRightStart].y, 1.0f, rStart);

    uint32_t curLeftSkirtIdx  = acc.addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxLeftStart].x * sL0,
                                              cleanPts[startIdx].y + vertsStart[sc0.idxLeftStart].y * sL0,
                                              static_cast<float>(sL0), rStart);
    uint32_t curRightSkirtIdx = acc.addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxRightStart].x * sR0,
                                              cleanPts[startIdx].y + vertsStart[sc0.idxRightStart].y * sR0,
                                              static_cast<float>(sR0), rStart);

    emitPolygonFan(curCenterIdx, cleanPts[startIdx], rStart,
                   sc0.idxRightStart, sc0.idxLeftStart,
                   curRightIdx, curRightSkirtIdx,
                   curLeftIdx, curLeftSkirtIdx,
                   -s0.ux, -s0.uy, 1.0f);

    // Traverse segments and emit quads + corner fans
    for (size_t si = 0; si < numSubSegs; ++si) {
        size_t segIdx = startIdx + si;
        size_t nextPtIdx = segIdx + 1;
        float rNext = std::max(0.0f, cleanPts[nextPtIdx].r);
        auto vertsNext = GetNibVertices(capType, rNext);
        const auto& curSc = segCorners[si];

        double lenLE = std::hypot(vertsNext[curSc.idxLeftEnd].x, vertsNext[curSc.idxLeftEnd].y);
        double sLE = (lenLE > 1e-4) ? (1.0 + delta / lenLE) : 1.0;
        double lenRE = std::hypot(vertsNext[curSc.idxRightEnd].x, vertsNext[curSc.idxRightEnd].y);
        double sRE = (lenRE > 1e-4) ? (1.0 + delta / lenRE) : 1.0;

        uint32_t nextLeftIdx   = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxLeftEnd].x,
                                               cleanPts[nextPtIdx].y + vertsNext[curSc.idxLeftEnd].y, 1.0f, rNext);
        uint32_t nextCenterIdx = acc.addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
        uint32_t nextRightIdx  = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxRightEnd].x,
                                               cleanPts[nextPtIdx].y + vertsNext[curSc.idxRightEnd].y, 1.0f, rNext);

        uint32_t nextLeftSkirtIdx  = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxLeftEnd].x * sLE,
                                                   cleanPts[nextPtIdx].y + vertsNext[curSc.idxLeftEnd].y * sLE,
                                                   static_cast<float>(sLE), rNext);
        uint32_t nextRightSkirtIdx = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxRightEnd].x * sRE,
                                                   cleanPts[nextPtIdx].y + vertsNext[curSc.idxRightEnd].y * sRE,
                                                   static_cast<float>(sRE), rNext);

        // Connect core quads along segment (CCW in screen space)
        acc.addCoreTriangle(curLeftIdx, nextCenterIdx, curCenterIdx);
        acc.addCoreTriangle(curLeftIdx, nextLeftIdx, nextCenterIdx);

        acc.addCoreTriangle(curCenterIdx, nextRightIdx, curRightIdx);
        acc.addCoreTriangle(curCenterIdx, nextCenterIdx, nextRightIdx);

        // Connect skirt quads along segment
        acc.addSkirtTriangle(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx);
        acc.addSkirtTriangle(curLeftIdx, nextLeftSkirtIdx, nextLeftIdx);

        acc.addSkirtTriangle(curRightIdx, nextRightIdx, nextRightSkirtIdx);
        acc.addSkirtTriangle(curRightIdx, nextRightSkirtIdx, curRightSkirtIdx);

        if (si + 1 == numSubSegs) {
            // Tail Cap at endIdx: covers front-facing corners of the fixed nib
            const auto& lastSeg = segs[segIdx];
            emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                           curSc.idxLeftEnd, curSc.idxRightEnd,
                           nextLeftIdx, nextLeftSkirtIdx,
                           nextRightIdx, nextRightSkirtIdx,
                           lastSeg.ux, lastSeg.uy, 1.0f);
            break;
        }

        // Interior joint P_{nextPtIdx}: transition between segment si and segment si+1
        const auto& nextSc = segCorners[si + 1];
        const auto& sIn    = segs[segIdx];
        const auto& sOut   = segs[segIdx + 1];

        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        const bool leftIsOuter  = (cross >  1e-4);
        const bool rightIsOuter = (cross < -1e-4);

        uint32_t outLeftStartIdx = nextLeftIdx;
        uint32_t outLeftStartSkirtIdx = nextLeftSkirtIdx;
        if (curSc.idxLeftEnd != nextSc.idxLeftStart) {
            double lenLS = std::hypot(vertsNext[nextSc.idxLeftStart].x, vertsNext[nextSc.idxLeftStart].y);
            double sLS = (lenLS > 1e-4) ? (1.0 + delta / lenLS) : 1.0;
            outLeftStartIdx = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxLeftStart].x,
                                            cleanPts[nextPtIdx].y + vertsNext[nextSc.idxLeftStart].y, 1.0f, rNext);
            outLeftStartSkirtIdx = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxLeftStart].x * sLS,
                                                 cleanPts[nextPtIdx].y + vertsNext[nextSc.idxLeftStart].y * sLS,
                                                 static_cast<float>(sLS), rNext);
            if (leftIsOuter) {
                emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                               curSc.idxLeftEnd, nextSc.idxLeftStart,
                               nextLeftIdx, nextLeftSkirtIdx,
                               outLeftStartIdx, outLeftStartSkirtIdx,
                               sIn.nx + sOut.nx, sIn.ny + sOut.ny, 1.0f);
            } else {
                acc.addCoreTriangle(nextCenterIdx, nextLeftIdx, outLeftStartIdx);
            }
        }

        uint32_t outRightStartIdx = nextRightIdx;
        uint32_t outRightStartSkirtIdx = nextRightSkirtIdx;
        if (curSc.idxRightEnd != nextSc.idxRightStart) {
            double lenRS = std::hypot(vertsNext[nextSc.idxRightStart].x, vertsNext[nextSc.idxRightStart].y);
            double sRS = (lenRS > 1e-4) ? (1.0 + delta / lenRS) : 1.0;
            outRightStartIdx = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxRightStart].x,
                                             cleanPts[nextPtIdx].y + vertsNext[nextSc.idxRightStart].y, 1.0f, rNext);
            outRightStartSkirtIdx = acc.addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxRightStart].x * sRS,
                                                  cleanPts[nextPtIdx].y + vertsNext[nextSc.idxRightStart].y * sRS,
                                                  static_cast<float>(sRS), rNext);
            if (rightIsOuter) {
                emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                               curSc.idxRightEnd, nextSc.idxRightStart,
                               nextRightIdx, nextRightSkirtIdx,
                               outRightStartIdx, outRightStartSkirtIdx,
                               -(sIn.nx + sOut.nx), -(sIn.ny + sOut.ny), 1.0f);
            } else {
                acc.addCoreTriangle(nextCenterIdx, nextRightIdx, outRightStartIdx);
            }
        }

        curLeftIdx        = outLeftStartIdx;
        curLeftSkirtIdx   = outLeftStartSkirtIdx;
        curCenterIdx      = nextCenterIdx;
        curRightIdx       = outRightStartIdx;
        curRightSkirtIdx  = outRightStartSkirtIdx;
    }
}

} // namespace InkInternal
} // namespace Folio
