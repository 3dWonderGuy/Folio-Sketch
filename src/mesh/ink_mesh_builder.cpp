/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_builder.cpp
 * @brief Top-Level GPU Stroke Mesh Tessellator, Hairpin Splitter, and Sub-Ribbon Dispatcher
 * =========================================================================================
 */

#include "folio_sketch/ink_mesh.hpp"
#include "folio_sketch/ink_config.hpp"
#include "geometry/ink_stroke_filter.hpp"
#include "mesh/ink_mesh_internal.hpp"
#include <algorithm>
#include <vector>

namespace Folio {

using namespace Folio::InkInternal;

namespace {
MeshTessellationParams s_tessParams{};
} // namespace

void InkMeshBuilder::SetTessellationParams(const MeshTessellationParams& params) noexcept {
    s_tessParams = params;
}

const MeshTessellationParams& InkMeshBuilder::GetTessellationParams() noexcept {
    return s_tessParams;
}

void InkMeshBuilder::BuildSolidMesh(std::span<const StrokePoint> points,
                                    CapType capType,
                                    uint32_t color,
                                    InkMesh& outMesh,
                                    float scalePxPerMm) {
    outMesh.clear();
    if (points.empty()) return;

    constexpr float hairpinCosThreshold = InkingConfig::kDefaultHairpinCosThreshold;
    constexpr float hairpinMinRatio     = InkingConfig::kDefaultHairpinMinSegmentRadiusRatio;

    // 1. Condition input points: prune submerged containment disks, micro-steps, and regularize radius
    std::vector<StrokePoint> cleanPts = StrokeFilter::PreFilterPoints(points, s_tessParams.stationaryEnvelopment);
    StrokeFilter::RegularizeRadiusSlope(cleanPts);

    if (cleanPts.empty()) return;

    // Exterior Anti-Aliasing Skirt Width: 3 screen pixels wide in world millimeters for soft anti-aliasing
    const float effectiveScale = scalePxPerMm > 0.001f ? scalePxPerMm : 3.7795f;
    const float delta = 3.0f / effectiveScale;

    InkMeshAccumulator acc(outMesh, color, delta);

    // 2. Single Point: Emit smooth round circle dot or oriented polygonal nib with exterior skirt
    if (cleanPts.size() == 1) {
        EmitSinglePointMesh(cleanPts[0], capType, acc);
        return;
    }

    const size_t N = cleanPts.size();
    auto segs = ComputeSegments(cleanPts);
    if (segs.empty()) return;

    // 3. Hairpin Split Detection:
    // When turn angle is a sharp acute reversal (> 150° turn, dot < hairpinCosThreshold),
    // split only if adjacent segments are sufficiently long (> hairpinMinRatio * radius).
    std::vector<size_t> splitIndices;
    for (size_t i = 1; i < N - 1; ++i) {
        float rPivot = std::max(0.01f, cleanPts[i].r);
        if (segs[i - 1].len < hairpinMinRatio * rPivot || segs[i].len < hairpinMinRatio * rPivot) {
            continue;
        }
        double dot = segs[i - 1].ux * segs[i].ux + segs[i - 1].uy * segs[i].uy;
        if (dot < hairpinCosThreshold) {
            splitIndices.push_back(i);
        }
    }

    const double gentleAngleLimit = static_cast<double>(s_tessParams.cornerAngleThresholdRad);
    const double miterMaxClamp    = static_cast<double>(s_tessParams.miterMaxClampRatio);

    // 4. Execute sub-ribbon generation across hairpin split points
    outMesh.reserve(N * 10, (N - 1) * 24 + 128);

    size_t subStart = 0;
    for (size_t splitIdx : splitIndices) {
        if (splitIdx > subStart) {
            if (capType == CapType::Round) {
                BuildRoundSubRibbon(cleanPts, segs, subStart, splitIdx, gentleAngleLimit, miterMaxClamp, acc);
            } else {
                BuildPolygonalSubRibbon(cleanPts, segs, subStart, splitIdx, capType, acc);
            }
            subStart = splitIdx;
        }
    }
    if (subStart < N - 1) {
        if (capType == CapType::Round) {
            BuildRoundSubRibbon(cleanPts, segs, subStart, N - 1, gentleAngleLimit, miterMaxClamp, acc);
        } else {
            BuildPolygonalSubRibbon(cleanPts, segs, subStart, N - 1, capType, acc);
        }
    }

    acc.finalizeMesh();
}

} // namespace Folio
