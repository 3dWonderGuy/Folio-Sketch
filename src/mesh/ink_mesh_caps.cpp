/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_caps.cpp
 * @brief Single-Point Stamp and Semicircular Round Endcap Mesh Emitters
 * =========================================================================================
 */

#include "mesh/ink_mesh_internal.hpp"

namespace Folio {
namespace InkInternal {

void EmitSinglePointMesh(const StrokePoint& p, CapType capType, InkMeshAccumulator& acc) {
    float r = std::max(0.0f, p.r);
    if (r <= 0.0f) return;

    const float delta = acc.delta;

    if (capType == CapType::Square || capType == CapType::Chisel) {
        auto verts = GetNibVertices(capType, r);
        const uint32_t K = static_cast<uint32_t>(verts.size());
        uint32_t centerIdx = acc.addVertex(p.x, p.y, 0.0f, r);
        uint32_t baseRim = static_cast<uint32_t>(acc.outMesh.vertices.size());
        for (uint32_t k = 0; k < K; ++k) {
            acc.addVertex(p.x + verts[k].x, p.y + verts[k].y, 1.0f, r);
        }
        uint32_t skirtRim = static_cast<uint32_t>(acc.outMesh.vertices.size());
        for (uint32_t k = 0; k < K; ++k) {
            double len = std::hypot(verts[k].x, verts[k].y);
            double scaleK = (len > 1e-4) ? (1.0 + delta / len) : 1.0;
            acc.addVertex(p.x + verts[k].x * scaleK, p.y + verts[k].y * scaleK, static_cast<float>(scaleK), r);
        }
        for (uint32_t k = 0; k < K; ++k) {
            uint32_t nextK = (k + 1) % K;
            acc.addCoreTriangle(centerIdx, baseRim + k, baseRim + nextK);
            acc.addSkirtTriangle(baseRim + k, skirtRim + k, skirtRim + nextK);
            acc.addSkirtTriangle(baseRim + k, skirtRim + nextK, baseRim + nextK);
        }
        acc.finalizeMesh();
        return;
    }

    // Smooth round circle dot (32 steps) with exterior skirt
    constexpr int steps = 32;
    acc.outMesh.reserve(1 + steps * 2, steps * 3 + steps * 6);
    uint32_t centerIdx = acc.addVertex(p.x, p.y, 0.0f, r);
    uint32_t baseRim = static_cast<uint32_t>(acc.outMesh.vertices.size());
    for (int i = 0; i < steps; ++i) {
        double angle = 2.0 * M_PI * i / steps;
        acc.addVertex(p.x + std::cos(angle) * r, p.y + std::sin(angle) * r, 1.0f, r);
    }
    uint32_t skirtRim = static_cast<uint32_t>(acc.outMesh.vertices.size());
    float skirtDist = 1.0f + delta / r;
    for (int i = 0; i < steps; ++i) {
        double angle = 2.0 * M_PI * i / steps;
        acc.addVertex(p.x + std::cos(angle) * (r + delta), p.y + std::sin(angle) * (r + delta), skirtDist, r);
    }
    for (int i = 0; i < steps; ++i) {
        int nextI = (i + 1) % steps;
        acc.addCoreTriangle(centerIdx, baseRim + i, baseRim + nextI);
        acc.addSkirtTriangle(baseRim + i, skirtRim + i, skirtRim + nextI);
        acc.addSkirtTriangle(baseRim + i, skirtRim + nextI, baseRim + nextI);
    }
    acc.finalizeMesh();
}

void EmitRoundHeadCap(const StrokePoint& startPt,
                      const SegmentInfo& s0,
                      float rStart,
                      float skirtDistStart,
                      uint32_t curCenterIdx,
                      uint32_t curLeftIdx,
                      uint32_t curLeftSkirtIdx,
                      uint32_t curRightIdx,
                      uint32_t curRightSkirtIdx,
                      InkMeshAccumulator& acc) {
    constexpr int headSteps = 16;
    const float delta = acc.delta;
    uint32_t prevFanNom = curRightIdx;
    uint32_t prevFanSkirt = curRightSkirtIdx;
    for (int s = 1; s < headSteps; ++s) {
        double theta = M_PI * (static_cast<double>(s) / headSteps);
        double cosT = std::cos(theta);
        double sinT = std::sin(theta);
        double dx = -s0.nx * cosT - s0.ux * sinT;
        double dy = -s0.ny * cosT - s0.uy * sinT;

        uint32_t arcNomIdx = acc.addVertex(startPt.x + dx * rStart,
                                           startPt.y + dy * rStart,
                                           1.0f, rStart);
        uint32_t arcSkirtIdx = acc.addVertex(startPt.x + dx * (rStart + delta),
                                             startPt.y + dy * (rStart + delta),
                                             skirtDistStart, rStart);
        acc.addCoreTriangle(curCenterIdx, prevFanNom, arcNomIdx);
        acc.addSkirtTriangle(prevFanNom, prevFanSkirt, arcSkirtIdx);
        acc.addSkirtTriangle(prevFanNom, arcSkirtIdx, arcNomIdx);
        prevFanNom = arcNomIdx;
        prevFanSkirt = arcSkirtIdx;
    }
    acc.addCoreTriangle(curCenterIdx, prevFanNom, curLeftIdx);
    acc.addSkirtTriangle(prevFanNom, prevFanSkirt, curLeftSkirtIdx);
    acc.addSkirtTriangle(prevFanNom, curLeftSkirtIdx, curLeftIdx);
}

void EmitRoundTailCap(const StrokePoint& endPt,
                      const SegmentInfo& lastSeg,
                      float rNext,
                      float skirtDistNext,
                      uint32_t nextCenterIdx,
                      uint32_t nextLeftIdx,
                      uint32_t nextLeftSkirtIdx,
                      uint32_t nextRightIdx,
                      uint32_t nextRightSkirtIdx,
                      InkMeshAccumulator& acc) {
    constexpr int tailSteps = 16;
    const float delta = acc.delta;

    double relLX = acc.outMesh.vertices[nextLeftIdx].x - endPt.x;
    double relLY = acc.outMesh.vertices[nextLeftIdx].y - endPt.y;
    double thetaMin = std::atan2(relLX * lastSeg.ux + relLY * lastSeg.uy,
                                 relLX * lastSeg.nx + relLY * lastSeg.ny);

    double relRX = acc.outMesh.vertices[nextRightIdx].x - endPt.x;
    double relRY = acc.outMesh.vertices[nextRightIdx].y - endPt.y;
    double thetaMax = std::atan2(relRX * lastSeg.ux + relRY * lastSeg.uy,
                                 relRX * lastSeg.nx + relRY * lastSeg.ny);
    if (thetaMax <= 0.0) thetaMax = M_PI;

    uint32_t prevFanNom = nextLeftIdx;
    uint32_t prevFanSkirt = nextLeftSkirtIdx;
    for (int s = 1; s < tailSteps; ++s) {
        double theta = M_PI * (static_cast<double>(s) / tailSteps);
        if (theta <= thetaMin + 0.05 || theta >= thetaMax - 0.05) continue;
        double cosT = std::cos(theta);
        double sinT = std::sin(theta);
        double dx = lastSeg.nx * cosT + lastSeg.ux * sinT;
        double dy = lastSeg.ny * cosT + lastSeg.uy * sinT;

        uint32_t arcNomIdx = acc.addVertex(endPt.x + dx * rNext,
                                           endPt.y + dy * rNext,
                                           1.0f, rNext);
        uint32_t arcSkirtIdx = acc.addVertex(endPt.x + dx * (rNext + delta),
                                             endPt.y + dy * (rNext + delta),
                                             skirtDistNext, rNext);
        acc.addCoreTriangle(nextCenterIdx, prevFanNom, arcNomIdx);
        acc.addSkirtTriangle(prevFanNom, prevFanSkirt, arcSkirtIdx);
        acc.addSkirtTriangle(prevFanNom, arcSkirtIdx, arcNomIdx);
        prevFanNom = arcNomIdx;
        prevFanSkirt = arcSkirtIdx;
    }
    acc.addCoreTriangle(nextCenterIdx, prevFanNom, nextRightIdx);
    acc.addSkirtTriangle(prevFanNom, prevFanSkirt, nextRightSkirtIdx);
    acc.addSkirtTriangle(prevFanNom, nextRightSkirtIdx, nextRightIdx);
}

} // namespace InkInternal
} // namespace Folio
