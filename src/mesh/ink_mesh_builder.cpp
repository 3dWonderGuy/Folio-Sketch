#include "folio_sketch/ink_mesh.hpp"
#include "folio_sketch/ink_config.hpp"
#include "geometry/ink_nib_geometry.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include "geometry/ink_stroke_filter.hpp"
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
                                    InkMesh& outMesh,
                                    float scalePxPerMm) {
    outMesh.clear();
    if (points.empty()) return;

    // Use centralized constant thresholds from InkingConfig
    constexpr float hairpinCosThreshold = InkingConfig::kDefaultHairpinCosThreshold;
    constexpr float hairpinMinRatio     = InkingConfig::kDefaultHairpinMinSegmentRadiusRatio;

    // 1. Condition input points: prune submerged containment disks, micro-steps, and regularize radius
    std::vector<StrokePoint> cleanPts = StrokeFilter::PreFilterPoints(points);
    StrokeFilter::RegularizeRadiusSlope(cleanPts);

    if (cleanPts.empty()) return;

    // Exterior Anti-Aliasing Skirt Width: 3 screen pixels wide in world millimeters for soft anti-aliasing
    const float effectiveScale = scalePxPerMm > 0.001f ? scalePxPerMm : 3.7795f;
    const float delta = 3.0f / effectiveScale;

    // Partitioned index buffers: Core indices first, AA Skirt indices second
    std::vector<uint32_t> coreIndices;
    std::vector<uint32_t> skirtIndices;

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

    auto addCoreTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        coreIndices.push_back(i0);
        coreIndices.push_back(i1);
        coreIndices.push_back(i2);
    };

    auto addSkirtTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        skirtIndices.push_back(i0);
        skirtIndices.push_back(i1);
        skirtIndices.push_back(i2);
    };

    auto finalizeMesh = [&]() {
        outMesh.indices = std::move(coreIndices);
        outMesh.coreIndexCount = static_cast<uint32_t>(outMesh.indices.size());
        outMesh.indices.insert(outMesh.indices.end(), skirtIndices.begin(), skirtIndices.end());
    };

    // 2. Single Point: Emit smooth round circle dot or oriented polygonal nib with 1-pixel exterior skirt
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
            uint32_t skirtRim = static_cast<uint32_t>(outMesh.vertices.size());
            for (uint32_t k = 0; k < K; ++k) {
                double len = std::hypot(verts[k].x, verts[k].y);
                double scaleK = (len > 1e-4) ? (1.0 + delta / len) : 1.0;
                addVertex(p.x + verts[k].x * scaleK, p.y + verts[k].y * scaleK, static_cast<float>(scaleK), r);
            }
            for (uint32_t k = 0; k < K; ++k) {
                uint32_t nextK = (k + 1) % K;
                addCoreTriangle(centerIdx, baseRim + k, baseRim + nextK);
                addSkirtTriangle(baseRim + k, skirtRim + k, skirtRim + nextK);
                addSkirtTriangle(baseRim + k, skirtRim + nextK, baseRim + nextK);
            }
            finalizeMesh();
            return;
        }

        // Smooth round circle dot (32 steps) with exterior skirt
        constexpr int steps = 32;
        outMesh.reserve(1 + steps * 2, steps * 3 + steps * 6);
        uint32_t centerIdx = addVertex(p.x, p.y, 0.0f, r);
        uint32_t baseRim = static_cast<uint32_t>(outMesh.vertices.size());
        for (int i = 0; i < steps; ++i) {
            double angle = 2.0 * M_PI * i / steps;
            addVertex(p.x + std::cos(angle) * r, p.y + std::sin(angle) * r, 1.0f, r);
        }
        uint32_t skirtRim = static_cast<uint32_t>(outMesh.vertices.size());
        float skirtDist = 1.0f + delta / r;
        for (int i = 0; i < steps; ++i) {
            double angle = 2.0 * M_PI * i / steps;
            addVertex(p.x + std::cos(angle) * (r + delta), p.y + std::sin(angle) * (r + delta), skirtDist, r);
        }
        for (int i = 0; i < steps; ++i) {
            int nextI = (i + 1) % steps;
            addCoreTriangle(centerIdx, baseRim + i, baseRim + nextI);
            addSkirtTriangle(baseRim + i, skirtRim + i, skirtRim + nextI);
            addSkirtTriangle(baseRim + i, skirtRim + nextI, baseRim + nextI);
        }
        finalizeMesh();
        return;
    }

    const size_t N = cleanPts.size();
    auto segs = ComputeSegments(cleanPts);
    if (segs.empty()) return;

    // -------------------------------------------------------------------------
    // HAIRPIN SPLIT DETECTION:
    // When turn angle is a sharp acute reversal (> 150° turn, dot < hairpinCosThreshold),
    // normal vectors invert into bowtie knots.
    // We only split if adjacent segments are sufficiently long (> hairpinMinRatio * radius)
    // so micro-jitter from fast sampling or physics springs does not false-trigger.
    // -------------------------------------------------------------------------
    std::vector<size_t> splitIndices;
    for (size_t i = 1; i < N - 1; ++i) {
        float rPivot = std::max(0.01f, cleanPts[i].r);
        // Ignore micro-segments where directions are unstable noise
        if (segs[i - 1].len < hairpinMinRatio * rPivot || segs[i].len < hairpinMinRatio * rPivot) {
            continue;
        }
        double dot = segs[i - 1].ux * segs[i].ux + segs[i - 1].uy * segs[i].uy;
        if (dot < hairpinCosThreshold) {
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
        float skirtDistStart = 1.0f + delta / std::max(1e-4f, rStart);

        uint32_t curLeftIdx   = addVertex(cleanPts[startIdx].x + s0.nx * rStart,
                                          cleanPts[startIdx].y + s0.ny * rStart, 1.0f, rStart);
        uint32_t curCenterIdx = addVertex(cleanPts[startIdx].x, cleanPts[startIdx].y, 0.0f, rStart);
        uint32_t curRightIdx  = addVertex(cleanPts[startIdx].x - s0.nx * rStart,
                                          cleanPts[startIdx].y - s0.ny * rStart, 1.0f, rStart);

        uint32_t curLeftSkirtIdx  = addVertex(cleanPts[startIdx].x + s0.nx * (rStart + delta),
                                              cleanPts[startIdx].y + s0.ny * (rStart + delta), skirtDistStart, rStart);
        uint32_t curRightSkirtIdx = addVertex(cleanPts[startIdx].x - s0.nx * (rStart + delta),
                                              cleanPts[startIdx].y - s0.ny * (rStart + delta), skirtDistStart, rStart);

        // Head Endcap at startIdx: smooth round semicircle facing backward (-s0.u) with exterior skirt
        constexpr int headSteps = 16;
        uint32_t prevFanNom = curRightIdx;
        uint32_t prevFanSkirt = curRightSkirtIdx;
        for (int s = 1; s < headSteps; ++s) {
            double theta = M_PI * (static_cast<double>(s) / headSteps);
            double cosT = std::cos(theta);
            double sinT = std::sin(theta);
            double dx = -s0.nx * cosT - s0.ux * sinT;
            double dy = -s0.ny * cosT - s0.uy * sinT;

            uint32_t arcNomIdx = addVertex(cleanPts[startIdx].x + dx * rStart,
                                           cleanPts[startIdx].y + dy * rStart,
                                           1.0f, rStart);
            uint32_t arcSkirtIdx = addVertex(cleanPts[startIdx].x + dx * (rStart + delta),
                                             cleanPts[startIdx].y + dy * (rStart + delta),
                                             skirtDistStart, rStart);
            addCoreTriangle(curCenterIdx, prevFanNom, arcNomIdx);
            addSkirtTriangle(prevFanNom, prevFanSkirt, arcSkirtIdx);
            addSkirtTriangle(prevFanNom, arcSkirtIdx, arcNomIdx);
            prevFanNom = arcNomIdx;
            prevFanSkirt = arcSkirtIdx;
        }
        addCoreTriangle(curCenterIdx, prevFanNom, curLeftIdx);
        addSkirtTriangle(prevFanNom, prevFanSkirt, curLeftSkirtIdx);
        addSkirtTriangle(prevFanNom, curLeftSkirtIdx, curLeftIdx);

        // Traverse interior segments
        for (size_t i = startIdx; i < endIdx; ++i) {
            const size_t nextPtIdx = i + 1;
            float rNext = std::max(0.0f, cleanPts[nextPtIdx].r);
            float skirtDistNext = 1.0f + delta / std::max(1e-4f, rNext);

            if (nextPtIdx == endIdx) {
                // Terminal segment ending at endIdx
                const auto& lastSeg = segs[i];
                uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + lastSeg.nx * rNext,
                                                   cleanPts[nextPtIdx].y + lastSeg.ny * rNext, 1.0f, rNext);
                uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
                uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x - lastSeg.nx * rNext,
                                                   cleanPts[nextPtIdx].y - lastSeg.ny * rNext, 1.0f, rNext);

                uint32_t nextLeftSkirtIdx  = addVertex(cleanPts[nextPtIdx].x + lastSeg.nx * (rNext + delta),
                                                       cleanPts[nextPtIdx].y + lastSeg.ny * (rNext + delta), skirtDistNext, rNext);
                uint32_t nextRightSkirtIdx = addVertex(cleanPts[nextPtIdx].x - lastSeg.nx * (rNext + delta),
                                                       cleanPts[nextPtIdx].y - lastSeg.ny * (rNext + delta), skirtDistNext, rNext);

                // Core quads
                addCoreTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addCoreTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

                addCoreTriangle(curCenterIdx, curRightIdx, nextRightIdx);
                addCoreTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

                // Skirt quads
                addSkirtTriangle(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx);
                addSkirtTriangle(curLeftIdx, nextLeftSkirtIdx, nextLeftIdx);

                addSkirtTriangle(curRightIdx, nextRightIdx, nextRightSkirtIdx);
                addSkirtTriangle(curRightIdx, nextRightSkirtIdx, curRightSkirtIdx);

                // Tail Endcap at endIdx: smooth round semicircle facing forward (+lastSeg.u) with exterior skirt
                constexpr int tailSteps = 16;
                uint32_t prevFanNom = nextLeftIdx;
                uint32_t prevFanSkirt = nextLeftSkirtIdx;
                for (int s = 1; s < tailSteps; ++s) {
                    double theta = M_PI * (static_cast<double>(s) / tailSteps);
                    double cosT = std::cos(theta);
                    double sinT = std::sin(theta);
                    double dx = lastSeg.nx * cosT + lastSeg.ux * sinT;
                    double dy = lastSeg.ny * cosT + lastSeg.uy * sinT;

                    uint32_t arcNomIdx = addVertex(cleanPts[nextPtIdx].x + dx * rNext,
                                                   cleanPts[nextPtIdx].y + dy * rNext,
                                                   1.0f, rNext);
                    uint32_t arcSkirtIdx = addVertex(cleanPts[nextPtIdx].x + dx * (rNext + delta),
                                                     cleanPts[nextPtIdx].y + dy * (rNext + delta),
                                                     skirtDistNext, rNext);
                    addCoreTriangle(nextCenterIdx, prevFanNom, arcNomIdx);
                    addSkirtTriangle(prevFanNom, prevFanSkirt, arcSkirtIdx);
                    addSkirtTriangle(prevFanNom, arcSkirtIdx, arcNomIdx);
                    prevFanNom = arcNomIdx;
                    prevFanSkirt = arcSkirtIdx;
                }
                addCoreTriangle(nextCenterIdx, prevFanNom, nextRightIdx);
                addSkirtTriangle(prevFanNom, prevFanSkirt, nextRightSkirtIdx);
                addSkirtTriangle(prevFanNom, nextRightSkirtIdx, nextRightIdx);
                break;
            }

            // Interior vertex P_{i+1}
            const auto& sIn  = segs[i];
            const auto& sOut = segs[i + 1];

            double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
            double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
            double turnAngle = std::atan2(std::abs(cross), dot);

            if (turnAngle < 0.35) {
                // Gentle turn: continuous bisector miter with exterior skirts
                double avgNx = sIn.nx + sOut.nx;
                double avgNy = sIn.ny + sOut.ny;
                double avgLen = std::hypot(avgNx, avgNy);
                if (avgLen > 1e-6) { avgNx /= avgLen; avgNy /= avgLen; }
                else { avgNx = sIn.nx; avgNy = sIn.ny; }

                double miterScale = 1.0 / std::max(0.5, std::cos(turnAngle * 0.5));
                miterScale = std::min(miterScale, 1.25);

                uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + avgNx * rNext * miterScale,
                                                   cleanPts[nextPtIdx].y + avgNy * rNext * miterScale, 1.0f, rNext);
                uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
                uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x - avgNx * rNext * miterScale,
                                                   cleanPts[nextPtIdx].y - avgNy * rNext * miterScale, 1.0f, rNext);

                uint32_t nextLeftSkirtIdx  = addVertex(cleanPts[nextPtIdx].x + avgNx * (rNext * miterScale + delta),
                                                       cleanPts[nextPtIdx].y + avgNy * (rNext * miterScale + delta), skirtDistNext, rNext);
                uint32_t nextRightSkirtIdx = addVertex(cleanPts[nextPtIdx].x - avgNx * (rNext * miterScale + delta),
                                                       cleanPts[nextPtIdx].y - avgNy * (rNext * miterScale + delta), skirtDistNext, rNext);

                // Core quads
                addCoreTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addCoreTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

                addCoreTriangle(curCenterIdx, curRightIdx, nextRightIdx);
                addCoreTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

                // Skirt quads
                addSkirtTriangle(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx);
                addSkirtTriangle(curLeftIdx, nextLeftSkirtIdx, nextLeftIdx);

                addSkirtTriangle(curRightIdx, nextRightIdx, nextRightSkirtIdx);
                addSkirtTriangle(curRightIdx, nextRightSkirtIdx, curRightSkirtIdx);

                curLeftIdx        = nextLeftIdx;
                curLeftSkirtIdx   = nextLeftSkirtIdx;
                curCenterIdx      = nextCenterIdx;
                curRightIdx       = nextRightIdx;
                curRightSkirtIdx  = nextRightSkirtIdx;
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
            if (dot < -0.65 || avgLen < 1e-4) {
                // Acute hairpin turnaround: collapse inner miter distance to pivot cleanly on centerline
                innerMiterDist = 0.0;
            } else if (cosHalf > 1e-4) {
                innerMiterDist = std::min(rNext / cosHalf, static_cast<double>(rNext));
            }
            innerMiterDist = std::min(innerMiterDist, std::max(0.0, safeLimit));

            double innerSign = leftIsInner ? 1.0 : -1.0;
            double innerSkirtDist = std::min(static_cast<double>(innerMiterDist + delta), safeLimit);
            uint32_t innerIdx = addVertex(cleanPts[nextPtIdx].x + innerSign * avgNx * innerMiterDist,
                                          cleanPts[nextPtIdx].y + innerSign * avgNy * innerMiterDist,
                                          1.0f, rNext);
            uint32_t innerSkirtIdx = addVertex(cleanPts[nextPtIdx].x + innerSign * avgNx * innerSkirtDist,
                                               cleanPts[nextPtIdx].y + innerSign * avgNy * innerSkirtDist,
                                               skirtDistNext, rNext);

            double outerSign = leftIsInner ? -1.0 : 1.0;
            uint32_t outerInIdx = addVertex(cleanPts[nextPtIdx].x + outerSign * sIn.nx * rNext,
                                            cleanPts[nextPtIdx].y + outerSign * sIn.ny * rNext,
                                            1.0f, rNext);
            uint32_t outerInSkirtIdx = addVertex(cleanPts[nextPtIdx].x + outerSign * sIn.nx * (rNext + delta),
                                                 cleanPts[nextPtIdx].y + outerSign * sIn.ny * (rNext + delta),
                                                 skirtDistNext, rNext);

            if (leftIsInner) {
                // Left Core & Skirt (Inner)
                addCoreTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addCoreTriangle(curLeftIdx, nextCenterIdx, innerIdx);

                addSkirtTriangle(curLeftIdx, curLeftSkirtIdx, innerSkirtIdx);
                addSkirtTriangle(curLeftIdx, innerSkirtIdx, innerIdx);

                // Right Core & Skirt (Outer incoming)
                addCoreTriangle(curCenterIdx, curRightIdx, outerInIdx);
                addCoreTriangle(curCenterIdx, outerInIdx, nextCenterIdx);

                addSkirtTriangle(curRightIdx, outerInIdx, outerInSkirtIdx);
                addSkirtTriangle(curRightIdx, outerInSkirtIdx, curRightSkirtIdx);
            } else {
                // Left Core & Skirt (Outer incoming)
                addCoreTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
                addCoreTriangle(curLeftIdx, nextCenterIdx, outerInIdx);

                addSkirtTriangle(curLeftIdx, curLeftSkirtIdx, outerInSkirtIdx);
                addSkirtTriangle(curLeftIdx, outerInSkirtIdx, outerInIdx);

                // Right Core & Skirt (Inner)
                addCoreTriangle(curCenterIdx, curRightIdx, innerIdx);
                addCoreTriangle(curCenterIdx, innerIdx, nextCenterIdx);

                addSkirtTriangle(curRightIdx, innerIdx, innerSkirtIdx);
                addSkirtTriangle(curRightIdx, innerSkirtIdx, curRightSkirtIdx);
            }

            int fanSteps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 16.0))), 2, 16);
            double aStartFan = std::atan2(outerSign * sIn.ny,  outerSign * sIn.nx);
            double aEndFan   = std::atan2(outerSign * sOut.ny, outerSign * sOut.nx);
            // Ensure proper shortest angular direction based on turn side
            double sweepFan = aEndFan - aStartFan;
            if (leftIsInner) {
                while (sweepFan > 0.0) sweepFan -= 2.0 * M_PI;
                while (sweepFan < -2.0 * M_PI) sweepFan += 2.0 * M_PI;
            } else {
                while (sweepFan < 0.0) sweepFan += 2.0 * M_PI;
                while (sweepFan > 2.0 * M_PI) sweepFan -= 2.0 * M_PI;
            }

            uint32_t prevFanNom = outerInIdx;
            uint32_t prevFanSkirt = outerInSkirtIdx;
            for (int s = 1; s <= fanSteps; ++s) {
                double a = (s == fanSteps) ? aEndFan : (aStartFan + (static_cast<double>(s) / fanSteps) * sweepFan);
                uint32_t arcNomIdx = addVertex(cleanPts[nextPtIdx].x + std::cos(a) * rNext,
                                               cleanPts[nextPtIdx].y + std::sin(a) * rNext,
                                               1.0f, rNext);
                uint32_t arcSkirtIdx = addVertex(cleanPts[nextPtIdx].x + std::cos(a) * (rNext + delta),
                                                 cleanPts[nextPtIdx].y + std::sin(a) * (rNext + delta),
                                                 skirtDistNext, rNext);
                if (leftIsInner) {
                    addCoreTriangle(nextCenterIdx, arcNomIdx, prevFanNom);
                    addSkirtTriangle(prevFanNom, arcNomIdx, arcSkirtIdx);
                    addSkirtTriangle(prevFanNom, arcSkirtIdx, prevFanSkirt);
                } else {
                    addCoreTriangle(nextCenterIdx, prevFanNom, arcNomIdx);
                    addSkirtTriangle(prevFanNom, prevFanSkirt, arcSkirtIdx);
                    addSkirtTriangle(prevFanNom, arcSkirtIdx, arcNomIdx);
                }
                prevFanNom = arcNomIdx;
                prevFanSkirt = arcSkirtIdx;
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
    };

    // Helper to build a polygonal nib sub-ribbon (Square/Highlighter, Chisel/Calligraphy) with exterior skirts
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

        // Helper to emit a polygon fan between two corner indices at point P with exterior skirts
        auto emitPolygonFan = [&](uint32_t cIdx, const StrokePoint& pt, float r,
                                  int startCorner, int endCorner,
                                  uint32_t startVtxIdx, uint32_t startSkirtIdx,
                                  uint32_t endVtxIdx, uint32_t endSkirtIdx,
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

            uint32_t prevNom = startVtxIdx;
            uint32_t prevSkirt = startSkirtIdx;
            for (int idx : chosen) {
                double len = std::hypot(verts[idx].x, verts[idx].y);
                double s = (len > 1e-4) ? (1.0 + delta / len) : 1.0;
                uint32_t curNom = addVertex(pt.x + verts[idx].x, pt.y + verts[idx].y, edgeDist, r);
                uint32_t curSkirt = addVertex(pt.x + verts[idx].x * s, pt.y + verts[idx].y * s, edgeDist * static_cast<float>(s), r);
                addCoreTriangle(cIdx, prevNom, curNom);
                addSkirtTriangle(prevNom, prevSkirt, curSkirt);
                addSkirtTriangle(prevNom, curSkirt, curNom);
                prevNom = curNom;
                prevSkirt = curSkirt;
            }
            addCoreTriangle(cIdx, prevNom, endVtxIdx);
            addSkirtTriangle(prevNom, prevSkirt, endSkirtIdx);
            addSkirtTriangle(prevNom, endSkirtIdx, endVtxIdx);
        };

        // Head Cap at startIdx: covers rear-facing corners of the fixed nib
        float rStart = std::max(0.0f, cleanPts[startIdx].r);
        auto vertsStart = Folio::InkInternal::GetNibVertices(capType, rStart);
        const auto& sc0 = segCorners[0];
        const auto& s0  = segs[startIdx];

        double lenL0 = std::hypot(vertsStart[sc0.idxLeftStart].x, vertsStart[sc0.idxLeftStart].y);
        double sL0 = (lenL0 > 1e-4) ? (1.0 + delta / lenL0) : 1.0;
        double lenR0 = std::hypot(vertsStart[sc0.idxRightStart].x, vertsStart[sc0.idxRightStart].y);
        double sR0 = (lenR0 > 1e-4) ? (1.0 + delta / lenR0) : 1.0;

        uint32_t curLeftIdx   = addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxLeftStart].x,
                                          cleanPts[startIdx].y + vertsStart[sc0.idxLeftStart].y, 1.0f, rStart);
        uint32_t curCenterIdx = addVertex(cleanPts[startIdx].x, cleanPts[startIdx].y, 0.0f, rStart);
        uint32_t curRightIdx  = addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxRightStart].x,
                                          cleanPts[startIdx].y + vertsStart[sc0.idxRightStart].y, 1.0f, rStart);

        uint32_t curLeftSkirtIdx  = addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxLeftStart].x * sL0,
                                              cleanPts[startIdx].y + vertsStart[sc0.idxLeftStart].y * sL0,
                                              static_cast<float>(sL0), rStart);
        uint32_t curRightSkirtIdx = addVertex(cleanPts[startIdx].x + vertsStart[sc0.idxRightStart].x * sR0,
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
            auto vertsNext = Folio::InkInternal::GetNibVertices(capType, rNext);
            const auto& curSc = segCorners[si];

            double lenLE = std::hypot(vertsNext[curSc.idxLeftEnd].x, vertsNext[curSc.idxLeftEnd].y);
            double sLE = (lenLE > 1e-4) ? (1.0 + delta / lenLE) : 1.0;
            double lenRE = std::hypot(vertsNext[curSc.idxRightEnd].x, vertsNext[curSc.idxRightEnd].y);
            double sRE = (lenRE > 1e-4) ? (1.0 + delta / lenRE) : 1.0;

            uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxLeftEnd].x,
                                               cleanPts[nextPtIdx].y + vertsNext[curSc.idxLeftEnd].y, 1.0f, rNext);
            uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
            uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxRightEnd].x,
                                               cleanPts[nextPtIdx].y + vertsNext[curSc.idxRightEnd].y, 1.0f, rNext);

            uint32_t nextLeftSkirtIdx  = addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxLeftEnd].x * sLE,
                                                   cleanPts[nextPtIdx].y + vertsNext[curSc.idxLeftEnd].y * sLE,
                                                   static_cast<float>(sLE), rNext);
            uint32_t nextRightSkirtIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[curSc.idxRightEnd].x * sRE,
                                                   cleanPts[nextPtIdx].y + vertsNext[curSc.idxRightEnd].y * sRE,
                                                   static_cast<float>(sRE), rNext);

            // Connect core quads along segment
            addCoreTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
            addCoreTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

            addCoreTriangle(curCenterIdx, curRightIdx, nextRightIdx);
            addCoreTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

            // Connect skirt quads along segment
            addSkirtTriangle(curLeftIdx, curLeftSkirtIdx, nextLeftSkirtIdx);
            addSkirtTriangle(curLeftIdx, nextLeftSkirtIdx, nextLeftIdx);

            addSkirtTriangle(curRightIdx, nextRightIdx, nextRightSkirtIdx);
            addSkirtTriangle(curRightIdx, nextRightSkirtIdx, curRightSkirtIdx);

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

            // In screen coordinates (+Y down, normal = (+uy, -ux) pointing left):
            // cross > 0 represents a right turn where LEFT is outer and RIGHT is inner.
            // cross < 0 represents a left turn where RIGHT is outer and LEFT is inner.
            const bool leftIsOuter  = (cross >  1e-4);
            const bool rightIsOuter = (cross < -1e-4);

            uint32_t outLeftStartIdx = nextLeftIdx;
            uint32_t outLeftStartSkirtIdx = nextLeftSkirtIdx;
            if (curSc.idxLeftEnd != nextSc.idxLeftStart) {
                double lenLS = std::hypot(vertsNext[nextSc.idxLeftStart].x, vertsNext[nextSc.idxLeftStart].y);
                double sLS = (lenLS > 1e-4) ? (1.0 + delta / lenLS) : 1.0;
                outLeftStartIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxLeftStart].x,
                                            cleanPts[nextPtIdx].y + vertsNext[nextSc.idxLeftStart].y, 1.0f, rNext);
                outLeftStartSkirtIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxLeftStart].x * sLS,
                                                 cleanPts[nextPtIdx].y + vertsNext[nextSc.idxLeftStart].y * sLS,
                                                 static_cast<float>(sLS), rNext);
                if (leftIsOuter) {
                    emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                                   curSc.idxLeftEnd, nextSc.idxLeftStart,
                                   nextLeftIdx, nextLeftSkirtIdx,
                                   outLeftStartIdx, outLeftStartSkirtIdx,
                                   sIn.nx + sOut.nx, sIn.ny + sOut.ny, 1.0f);
                } else {
                    // Inner corner transition: connect directly across the joint to prevent inner rail crossing
                    addCoreTriangle(nextCenterIdx, nextLeftIdx, outLeftStartIdx);
                }
            }

            uint32_t outRightStartIdx = nextRightIdx;
            uint32_t outRightStartSkirtIdx = nextRightSkirtIdx;
            if (curSc.idxRightEnd != nextSc.idxRightStart) {
                double lenRS = std::hypot(vertsNext[nextSc.idxRightStart].x, vertsNext[nextSc.idxRightStart].y);
                double sRS = (lenRS > 1e-4) ? (1.0 + delta / lenRS) : 1.0;
                outRightStartIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxRightStart].x,
                                             cleanPts[nextPtIdx].y + vertsNext[nextSc.idxRightStart].y, 1.0f, rNext);
                outRightStartSkirtIdx = addVertex(cleanPts[nextPtIdx].x + vertsNext[nextSc.idxRightStart].x * sRS,
                                                  cleanPts[nextPtIdx].y + vertsNext[nextSc.idxRightStart].y * sRS,
                                                  static_cast<float>(sRS), rNext);
                if (rightIsOuter) {
                    emitPolygonFan(nextCenterIdx, cleanPts[nextPtIdx], rNext,
                                   curSc.idxRightEnd, nextSc.idxRightStart,
                                   nextRightIdx, nextRightSkirtIdx,
                                   outRightStartIdx, outRightStartSkirtIdx,
                                   -(sIn.nx + sOut.nx), -(sIn.ny + sOut.ny), 1.0f);
                } else {
                    // Inner corner transition: connect directly across the joint to prevent inner rail crossing
                    addCoreTriangle(nextCenterIdx, nextRightIdx, outRightStartIdx);
                }
            }

            curLeftIdx        = outLeftStartIdx;
            curLeftSkirtIdx   = outLeftStartSkirtIdx;
            curCenterIdx      = nextCenterIdx;
            curRightIdx       = outRightStartIdx;
            curRightSkirtIdx  = outRightStartSkirtIdx;
        }
    };

    // Execute sub-ribbon generation across hairpin split points
    outMesh.reserve(N * 10, (N - 1) * 24 + 128);

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

    finalizeMesh();
}

} // namespace Folio
