#include "folio_sketch/ink_mesh.hpp"
#include "geometry/ink_nib_geometry.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

using namespace Folio::InkInternal;

void InkMeshBuilder::BuildSolidMesh(std::span<const StrokePoint> points,
                                    CapType capType,
                                    uint32_t color,
                                    InkMesh& outMesh) {
    outMesh.clear();
    if (points.empty()) return;

    // 1. Pre-filter coincident micro-points (< 1e-4 mm separation)
    std::vector<StrokePoint> cleanPts;
    cleanPts.reserve(points.size());
    for (const auto& p : points) {
        if (cleanPts.empty()) {
            cleanPts.push_back(p);
        } else {
            double d = std::hypot(p.x - cleanPts.back().x, p.y - cleanPts.back().y);
            if (d >= 1e-4) {
                cleanPts.push_back(p);
            } else {
                cleanPts.back().r = std::max(cleanPts.back().r, p.r);
                cleanPts.back().pressure = std::max(cleanPts.back().pressure, p.pressure);
            }
        }
    }

    if (cleanPts.empty()) return;

    // Helper lambdas to append vertices and triangles
    auto addVertex = [&](double vx, double vy, float edgeDist, float radius) -> uint32_t {
        uint32_t idx = static_cast<uint32_t>(outMesh.vertices.size());
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(vx),
            .y = static_cast<float>(vy),
            .edgeDist = edgeDist,
            .radius = radius,
            .color = color
        });
        return idx;
    };

    auto addTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        outMesh.indices.push_back(i0);
        outMesh.indices.push_back(i1);
        outMesh.indices.push_back(i2);
    };

    // 2. Single Point: Emit smooth round circle dot or oriented polygonal nib
    if (cleanPts.size() == 1) {
        const auto& p = cleanPts[0];
        float r = std::max(0.0f, p.r);
        if (r <= 0.0f) return;

        if (capType == CapType::Square || capType == CapType::Chisel) {
            auto verts = Folio::InkInternal::GetNibVertices(capType, r);
            const uint32_t K = static_cast<uint32_t>(verts.size());
            uint32_t centerIdx = addVertex(p.x, p.y, 0.0f, r);
            uint32_t baseRim = static_cast<uint32_t>(outMesh.vertices.size());
            for (uint32_t k = 0; k < K; ++k) {
                addVertex(p.x + verts[k].x, p.y + verts[k].y, 1.0f, r);
            }
            for (uint32_t k = 0; k < K; ++k) {
                addTriangle(centerIdx, baseRim + k, baseRim + ((k + 1) % K));
            }
            return;
        }

        // Smooth round circle dot (32 steps)
        constexpr int steps = 32;
        outMesh.reserve(steps + 1, steps * 3);
        uint32_t centerIdx = addVertex(p.x, p.y, 0.0f, r);
        uint32_t firstRimIdx = static_cast<uint32_t>(outMesh.vertices.size());
        for (int i = 0; i < steps; ++i) {
            double angle = 2.0 * M_PI * i / steps;
            addVertex(p.x + std::cos(angle) * r, p.y + std::sin(angle) * r, 1.0f, r);
        }
        for (int i = 0; i < steps; ++i) {
            addTriangle(centerIdx, firstRimIdx + i, firstRimIdx + ((i + 1) % steps));
        }
        return;
    }

    const size_t N = cleanPts.size();
    auto segs = ComputeSegments(cleanPts);
    if (segs.empty()) return;

    // -------------------------------------------------------------------------
    // HAIRPIN SPLIT DETECTION:
    // When turn angle > 130° (dot < -0.65), normal vectors cancel out and ribbons
    // invert into bowtie knots. We split at sharp turnarounds into clean sub-ribbons
    // sharing the pivot vertex.
    // -------------------------------------------------------------------------
    std::vector<size_t> splitIndices;
    for (size_t i = 1; i < N - 1; ++i) {
        double dot = segs[i - 1].ux * segs[i].ux + segs[i - 1].uy * segs[i].uy;
        if (dot < -0.65) {
            splitIndices.push_back(i);
        }
    }

    // Helper to build a round sub-ribbon from cleanPts[startIdx ... endIdx]
    auto buildRoundSubRibbon = [&](size_t startIdx, size_t endIdx) {
        if (endIdx <= startIdx) return;
        const size_t subN = endIdx - startIdx + 1;
        if (subN < 2) return;

        float rStart = std::max(0.0f, cleanPts[startIdx].r);
        const auto& s0 = segs[startIdx];

        uint32_t curLeftIdx   = addVertex(cleanPts[startIdx].x + s0.nx * rStart,
                                          cleanPts[startIdx].y + s0.ny * rStart, -1.0f, rStart);
        uint32_t curCenterIdx = addVertex(cleanPts[startIdx].x, cleanPts[startIdx].y, 0.0f, rStart);
        uint32_t curRightIdx  = addVertex(cleanPts[startIdx].x - s0.nx * rStart,
                                          cleanPts[startIdx].y - s0.ny * rStart, 1.0f, rStart);

        // Head Endcap at startIdx: smooth round semicircle facing backward (-s0.u)
        constexpr int headSteps = 16;
        double aStart = std::atan2(-s0.ny, -s0.nx);
        double aEnd   = std::atan2( s0.ny,  s0.nx);
        double sweep  = aEnd - aStart;
        while (sweep < 0.0) sweep += 2.0 * M_PI;
        if (sweep > 2.0 * M_PI) sweep -= 2.0 * M_PI;

        uint32_t prevFan = curRightIdx;
        for (int s = 1; s < headSteps; ++s) {
            double a = aStart + (static_cast<double>(s) / headSteps) * sweep;
            uint32_t arcIdx = addVertex(cleanPts[startIdx].x + std::cos(a) * rStart,
                                        cleanPts[startIdx].y + std::sin(a) * rStart,
                                        1.0f, rStart);
            addTriangle(curCenterIdx, prevFan, arcIdx);
            prevFan = arcIdx;
        }
        addTriangle(curCenterIdx, prevFan, curLeftIdx);

        // Traverse interior segments
        for (size_t i = startIdx; i < endIdx; ++i) {
            const size_t nextPtIdx = i + 1;
            float rNext = std::max(0.0f, cleanPts[nextPtIdx].r);

            if (nextPtIdx == endIdx) {
                // Terminal segment ending at endIdx
                const auto& lastSeg = segs[i];
                uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + lastSeg.nx * rNext,
                                                   cleanPts[nextPtIdx].y + lastSeg.ny * rNext, -1.0f, rNext);
                uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
                uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x - lastSeg.nx * rNext,
                                                   cleanPts[nextPtIdx].y - lastSeg.ny * rNext, 1.0f, rNext);

                addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

                addTriangle(curCenterIdx, curRightIdx, nextRightIdx);
                addTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

                // Tail Endcap at endIdx: smooth round semicircle facing forward (+lastSeg.u)
                constexpr int tailSteps = 16;
                double aStartTail = std::atan2( lastSeg.ny,  lastSeg.nx);
                double aEndTail   = std::atan2(-lastSeg.ny, -lastSeg.nx);
                double sweepTail  = aEndTail - aStartTail;
                while (sweepTail < 0.0) sweepTail += 2.0 * M_PI;
                if (sweepTail > 2.0 * M_PI) sweepTail -= 2.0 * M_PI;

                uint32_t prevFanTail = nextLeftIdx;
                for (int s = 1; s < tailSteps; ++s) {
                    double a = aStartTail + (static_cast<double>(s) / tailSteps) * sweepTail;
                    uint32_t arcIdx = addVertex(cleanPts[nextPtIdx].x + std::cos(a) * rNext,
                                                cleanPts[nextPtIdx].y + std::sin(a) * rNext,
                                                1.0f, rNext);
                    addTriangle(nextCenterIdx, prevFanTail, arcIdx);
                    prevFanTail = arcIdx;
                }
                addTriangle(nextCenterIdx, prevFanTail, nextRightIdx);
                break;
            }

            // Interior vertex P_{i+1}
            const auto& sIn  = segs[i];
            const auto& sOut = segs[i + 1];

            double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
            double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
            double turnAngle = std::atan2(std::abs(cross), dot);

            if (turnAngle < 0.35) {
                // Gentle turn: continuous bisector miter
                double avgNx = sIn.nx + sOut.nx;
                double avgNy = sIn.ny + sOut.ny;
                double avgLen = std::hypot(avgNx, avgNy);
                if (avgLen > 1e-6) { avgNx /= avgLen; avgNy /= avgLen; }
                else { avgNx = sIn.nx; avgNy = sIn.ny; }

                double miterScale = 1.0 / std::max(0.5, std::cos(turnAngle * 0.5));
                miterScale = std::min(miterScale, 1.25);

                uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + avgNx * rNext * miterScale,
                                                   cleanPts[nextPtIdx].y + avgNy * rNext * miterScale, -1.0f, rNext);
                uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
                uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x - avgNx * rNext * miterScale,
                                                   cleanPts[nextPtIdx].y - avgNy * rNext * miterScale, 1.0f, rNext);

                addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

                addTriangle(curCenterIdx, curRightIdx, nextRightIdx);
                addTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

                curLeftIdx   = nextLeftIdx;
                curCenterIdx = nextCenterIdx;
                curRightIdx  = nextRightIdx;
                continue;
            }

            // Sharp turn (turnAngle >= 0.35 rad, dot >= -0.65):
            bool leftIsInner = (cross < 0.0) || (cross == 0.0 && dot < 0.0);
            uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);

            double avgNx = sIn.nx + sOut.nx;
            double avgNy = sIn.ny + sOut.ny;
            double avgLen = std::hypot(avgNx, avgNy);
            if (avgLen > 1e-6) { avgNx /= avgLen; avgNy /= avgLen; }
            else { avgNx = sIn.nx; avgNy = sIn.ny; }

            double sinHalf = std::sqrt(std::max(0.0, (1.0 - dot) * 0.5));
            double safeLimit = (std::min(sIn.len, sOut.len) * 0.5) / std::max(1e-4, sinHalf);
            double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
            double innerMiterDist = rNext;
            if (cosHalf > 1e-4) {
                innerMiterDist = std::min(rNext / cosHalf, 1.25 * rNext);
            }
            innerMiterDist = std::min(innerMiterDist, std::max(0.0, safeLimit));

            double innerSign = leftIsInner ? 1.0 : -1.0;
            uint32_t innerIdx = addVertex(cleanPts[nextPtIdx].x + innerSign * avgNx * innerMiterDist,
                                          cleanPts[nextPtIdx].y + innerSign * avgNy * innerMiterDist,
                                          leftIsInner ? -1.0f : 1.0f, rNext);

            double outerSign = leftIsInner ? -1.0 : 1.0;
            uint32_t outerInIdx = addVertex(cleanPts[nextPtIdx].x + outerSign * sIn.nx * rNext,
                                            cleanPts[nextPtIdx].y + outerSign * sIn.ny * rNext,
                                            leftIsInner ? 1.0f : -1.0f, rNext);

            if (leftIsInner) {
                addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addTriangle(curLeftIdx, nextCenterIdx, innerIdx);

                addTriangle(curCenterIdx, curRightIdx, outerInIdx);
                addTriangle(curCenterIdx, outerInIdx, nextCenterIdx);
            } else {
                addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addTriangle(curLeftIdx, nextCenterIdx, outerInIdx);

                addTriangle(curCenterIdx, curRightIdx, innerIdx);
                addTriangle(curCenterIdx, innerIdx, nextCenterIdx);
            }

            int fanSteps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 16.0))), 2, 16);
            double aStartFan = std::atan2(outerSign * sIn.ny,  outerSign * sIn.nx);
            double aEndFan   = std::atan2(outerSign * sOut.ny, outerSign * sOut.nx);
            double sweepFan  = aEndFan - aStartFan;

            if (leftIsInner) {
                while (sweepFan > 0.0) sweepFan -= 2.0 * M_PI;
            } else {
                while (sweepFan < 0.0) sweepFan += 2.0 * M_PI;
            }

            uint32_t prevFan = outerInIdx;
            float outerEdgeDist = leftIsInner ? 1.0f : -1.0f;
            for (int s = 1; s <= fanSteps; ++s) {
                double a = aStartFan + (static_cast<double>(s) / fanSteps) * sweepFan;
                uint32_t arcIdx = addVertex(cleanPts[nextPtIdx].x + std::cos(a) * rNext,
                                            cleanPts[nextPtIdx].y + std::sin(a) * rNext,
                                            outerEdgeDist, rNext);
                if (leftIsInner) {
                    addTriangle(nextCenterIdx, arcIdx, prevFan);
                } else {
                    addTriangle(nextCenterIdx, prevFan, arcIdx);
                }
                prevFan = arcIdx;
            }

            curCenterIdx = nextCenterIdx;
            if (leftIsInner) {
                curLeftIdx  = innerIdx;
                curRightIdx = prevFan;
            } else {
                curLeftIdx  = prevFan;
                curRightIdx = innerIdx;
            }
        }
    };

    // Helper to build a polygonal nib sub-ribbon (Square/Highlighter, Chisel/Calligraphy)
    auto buildPolygonalSubRibbon = [&](size_t startIdx, size_t endIdx) {
        if (endIdx <= startIdx) return;
        const size_t subN = endIdx - startIdx + 1;
        if (subN < 2) return;

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
            auto verts = Folio::InkInternal::GetNibVertices(capType, rAvg);
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

        // Helper to emit a polygon fan between two corner indices at point P
        auto emitPolygonFan = [&](uint32_t cIdx, const StrokePoint& pt, float r,
                                  int startCorner, int endCorner,
                                  uint32_t startVtxIdx, uint32_t endVtxIdx,
                                  double outwardX, double outwardY, float edgeDist) {
            if (startCorner == endCorner) return;
            auto verts = Folio::InkInternal::GetNibVertices(capType, r);
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

            uint32_t prev = startVtxIdx;
            for (int idx : chosen) {
                uint32_t cur = addVertex(pt.x + verts[idx].x, pt.y + verts[idx].y, edgeDist, r);
                addTriangle(cIdx, prev, cur);
                prev = cur;
            }
            addTriangle(cIdx, prev, endVtxIdx);
        };

        // Head Cap at startIdx: covers rear-facing corners of the fixed nib
        float rStart = std::max(0.0f, cleanPts[startIdx].r);
        auto vertsStart = Folio::InkInternal::GetNibVertices(capType, rStart);
        const auto& sc0 = segCorners[0];
        const auto& s0  = segs[startIdx];

        uint32_t curLeftIdx   = addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxLeftStart].x,
                                          cleanPts[startIdx].y + vertsStart[sc0.idxLeftStart].y, -1.0f, rStart);
        uint32_t curCenterIdx = addVertex(cleanPts[startIdx].x, cleanPts[startIdx].y, 0.0f, rStart);
        uint32_t curRightIdx  = addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxRightStart].x,
                                          cleanPts[startIdx].y + vertsStart[sc0.idxRightStart].y, 1.0f, rStart);

        emitPolygonFan(curCenterIdx, cleanPts[startIdx], rStart,
                       sc0.idxRightStart, sc0.idxLeftStart,
                       curRightIdx, curLeftIdx,
                       -s0.ux, -s0.uy, 1.0f);

        // Traverse segments and emit quads + corner fans
        for (size_t si = 0; si < numSubSegs; ++si) {
            size_t segIdx = startIdx + si;
            size_t nextPtIdx = segIdx + 1;
            float rNext = std::max(0.0f, cleanPts[nextPtIdx].r);
            auto vertsNext = Folio::InkInternal::GetNibVertices(capType, rNext);
            const auto& curSc = segCorners[si];

            uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxLeftEnd].x,
                                               cleanPts[nextPtIdx].y + vertsNext[curSc.idxLeftEnd].y, -1.0f, rNext);
            uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
            uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxRightEnd].x,
                                               cleanPts[nextPtIdx].y + vertsNext[curSc.idxRightEnd].y, 1.0f, rNext);

            // Connect quad along segment
            addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
            addTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

            addTriangle(curCenterIdx, curRightIdx, nextRightIdx);
            addTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

            if (si + 1 == numSubSegs) {
                // Tail Cap at endIdx: covers front-facing corners of the fixed nib
                const auto& lastSeg = segs[segIdx];
                emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                               curSc.idxLeftEnd, curSc.idxRightEnd,
                               nextLeftIdx, nextRightIdx,
                               lastSeg.ux, lastSeg.uy, 1.0f);
                break;
            }

            // Interior joint P_{nextPtIdx}: transition between segment si and segment si+1
            const auto& nextSc = segCorners[si + 1];
            const auto& sIn    = segs[segIdx];
            const auto& sOut   = segs[segIdx + 1];

            uint32_t outLeftStartIdx = nextLeftIdx;
            if (curSc.idxLeftEnd != nextSc.idxLeftStart) {
                outLeftStartIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxLeftStart].x,
                                            cleanPts[nextPtIdx].y + vertsNext[nextSc.idxLeftStart].y, -1.0f, rNext);
                emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                               curSc.idxLeftEnd, nextSc.idxLeftStart,
                               nextLeftIdx, outLeftStartIdx,
                               sIn.nx + sOut.nx, sIn.ny + sOut.ny, -1.0f);
            }

            uint32_t outRightStartIdx = nextRightIdx;
            if (curSc.idxRightEnd != nextSc.idxRightStart) {
                outRightStartIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxRightStart].x,
                                             cleanPts[nextPtIdx].y + vertsNext[nextSc.idxRightStart].y, 1.0f, rNext);
                emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                               curSc.idxRightEnd, nextSc.idxRightStart,
                               nextRightIdx, outRightStartIdx,
                               -(sIn.nx + sOut.nx), -(sIn.ny + sOut.ny), 1.0f);
            }

            curLeftIdx   = outLeftStartIdx;
            curCenterIdx = nextCenterIdx;
            curRightIdx  = outRightStartIdx;
        }
    };

    // Execute sub-ribbon generation across hairpin split points
    outMesh.reserve(N * 6, (N - 1) * 12 + 64);

    size_t subStart = 0;
    for (size_t splitIdx : splitIndices) {
        if (splitIdx > subStart) {
            if (capType == CapType::Round) {
                buildRoundSubRibbon(subStart, splitIdx);
            } else {
                buildPolygonalSubRibbon(subStart, splitIdx);
            }
            subStart = splitIdx;
        }
    }
    if (subStart < N - 1) {
        if (capType == CapType::Round) {
            buildRoundSubRibbon(subStart, N - 1);
        } else {
            buildPolygonalSubRibbon(subStart, N - 1);
        }
    }
}

} // namespace Folio
