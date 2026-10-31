#pragma once
/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_internal.hpp
 * @brief Internal GPU Mesh Accumulator and Sub-Ribbon Tessellation Primitives
 * =========================================================================================
 */

#include "folio_sketch/ink_mesh.hpp"
#include "folio_sketch/ink_config.hpp"
#include "geometry/ink_nib_geometry.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {
namespace InkInternal {

/**
 * @struct PinnedCornerSide
 * @brief Pre-computed shared inner corner pivot indices (nominal core and exterior AA skirt).
 */
struct PinnedCornerSide {
    uint32_t nomIdx   = UINT32_MAX;
    uint32_t skirtIdx = UINT32_MAX;
};

/**
 * @struct InkMeshAccumulator
 * @brief Partitioned vertex and index buffer builder separating opaque core and AA skirt triangles.
 */
struct InkMeshAccumulator {
    InkMesh&              outMesh;
    uint32_t              color;
    float                 delta;
    std::vector<uint32_t> coreIndices;
    std::vector<uint32_t> skirtIndices;

    InkMeshAccumulator(InkMesh& mesh, uint32_t rgbaColor, float skirtDelta) noexcept
        : outMesh(mesh), color(rgbaColor), delta(skirtDelta) {}

    uint32_t addVertex(double vx, double vy, float edgeDist, float radius) {
        uint32_t idx = static_cast<uint32_t>(outMesh.vertices.size());
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(vx),
            .y = static_cast<float>(vy),
            .edgeDist = edgeDist,
            .radius = radius,
            .color = color
        });
        return idx;
    }

    void addCoreTriangle(uint32_t i0, uint32_t i1, uint32_t i2) {
        coreIndices.push_back(i0);
        coreIndices.push_back(i1);
        coreIndices.push_back(i2);
    }

    void addSkirtTriangle(uint32_t i0, uint32_t i1, uint32_t i2) {
        skirtIndices.push_back(i0);
        skirtIndices.push_back(i1);
        skirtIndices.push_back(i2);
    }

    [[nodiscard]] double triSignedArea2(uint32_t i0, uint32_t i1, uint32_t i2) const noexcept {
        const auto& v0 = outMesh.vertices[i0];
        const auto& v1 = outMesh.vertices[i1];
        const auto& v2 = outMesh.vertices[i2];
        return static_cast<double>(v1.x - v0.x) * static_cast<double>(v2.y - v0.y) -
               static_cast<double>(v2.x - v0.x) * static_cast<double>(v1.y - v0.y);
    }

    void addCoreQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        double minAC = std::min(triSignedArea2(a, b, c), triSignedArea2(a, c, d));
        double minBD = std::min(triSignedArea2(a, b, d), triSignedArea2(b, c, d));
        if (minAC >= minBD) {
            addCoreTriangle(a, b, c);
            addCoreTriangle(a, c, d);
        } else {
            addCoreTriangle(a, b, d);
            addCoreTriangle(b, c, d);
        }
    }

    void addSkirtQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        double areaABC = triSignedArea2(a, b, c);
        double areaACD = triSignedArea2(a, c, d);
        double areaABD = triSignedArea2(a, b, d);
        double areaBCD = triSignedArea2(b, c, d);
        double minAC = std::min(areaABC, areaACD);
        double minBD = std::min(areaABD, areaBCD);
        if (minAC >= minBD) {
            if (areaABC > 1e-6) addSkirtTriangle(a, b, c);
            if (areaACD > 1e-6) addSkirtTriangle(a, c, d);
        } else {
            if (areaABD > 1e-6) addSkirtTriangle(a, b, d);
            if (areaBCD > 1e-6) addSkirtTriangle(b, c, d);
        }
    }

    void finalizeMesh() {
        outMesh.indices = std::move(coreIndices);
        outMesh.coreIndexCount = static_cast<uint32_t>(outMesh.indices.size());
        outMesh.indices.insert(outMesh.indices.end(), skirtIndices.begin(), skirtIndices.end());
    }
};

/**
 * @brief Emits a single-point circular dot or oriented polygonal nib stamp with exterior AA skirt.
 */
void EmitSinglePointMesh(const StrokePoint& p, CapType capType, InkMeshAccumulator& acc);

/**
 * @brief Emits a backward-facing semicircular head endcap fan at startPt.
 */
void EmitRoundHeadCap(const StrokePoint& startPt,
                      const SegmentInfo& s0,
                      float rStart,
                      float skirtDistStart,
                      uint32_t curCenterIdx,
                      uint32_t curLeftIdx,
                      uint32_t curLeftSkirtIdx,
                      uint32_t curRightIdx,
                      uint32_t curRightSkirtIdx,
                      InkMeshAccumulator& acc);

/**
 * @brief Emits a forward-facing semicircular tail endcap fan at endPt, clipped to terminal rail angles.
 */
void EmitRoundTailCap(const StrokePoint& endPt,
                      const SegmentInfo& lastSeg,
                      float rNext,
                      float skirtDistNext,
                      uint32_t nextCenterIdx,
                      uint32_t nextLeftIdx,
                      uint32_t nextLeftSkirtIdx,
                      uint32_t nextRightIdx,
                      uint32_t nextRightSkirtIdx,
                      InkMeshAccumulator& acc);

/**
 * @brief Pre-pass that resolves shared inner corner pivots for multi-sample turns and V-pockets.
 */
void ResolveRoundCornerPivots(const std::vector<StrokePoint>& cleanPts,
                              const std::vector<SegmentInfo>& segs,
                              size_t startIdx,
                              size_t endIdx,
                              double gentleAngleLimit,
                              InkMeshAccumulator& acc,
                              std::vector<PinnedCornerSide>& pinnedLeft,
                              std::vector<PinnedCornerSide>& pinnedRight);

/**
 * @brief Builds a continuous round-capped sub-ribbon over cleanPts[startIdx ... endIdx].
 */
void BuildRoundSubRibbon(const std::vector<StrokePoint>& cleanPts,
                         const std::vector<SegmentInfo>& segs,
                         size_t startIdx,
                         size_t endIdx,
                         double gentleAngleLimit,
                         double miterMaxClamp,
                         InkMeshAccumulator& acc);

/**
 * @brief Builds a polygonal nib sub-ribbon (Square/Highlighter, Chisel/Calligraphy) over cleanPts[startIdx ... endIdx].
 */
void BuildPolygonalSubRibbon(const std::vector<StrokePoint>& cleanPts,
                             const std::vector<SegmentInfo>& segs,
                             size_t startIdx,
                             size_t endIdx,
                             CapType capType,
                             InkMeshAccumulator& acc);

} // namespace InkInternal
} // namespace Folio
