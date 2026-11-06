/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_builder.cpp
 * @brief High-Performance GPU Capsule SDF Instance Streaming Pipeline
 * =========================================================================================
 */

#include "folio_sketch/gpu/ink_mesh.hpp"
#include <algorithm>
#include <vector>

namespace Folio {

void CapsuleBuilder::BuildCapsuleInstances(std::span<const StrokePoint> points,
                                           uint32_t colorRgba,
                                           std::vector<CapsuleInstance>& outInstances) {
    outInstances.clear();
    const size_t n = points.size();
    if (n == 0) return;

    if (n == 1) {
        const auto& p0 = points[0];
        const float x0 = static_cast<float>(p0.x);
        const float y0 = static_cast<float>(p0.y);
        const float r0 = p0.r > 0.0f ? p0.r : 0.0f;
        outInstances.push_back(CapsuleInstance{
            CapsuleVec2(x0, y0),
            CapsuleVec2(x0, y0),
            CapsuleVec2(r0, r0),
            colorRgba,
            1.0f
        });
        return;
    }

    AppendCapsuleInstances(points, 0, colorRgba, outInstances);
}

void CapsuleBuilder::AppendCapsuleInstances(std::span<const StrokePoint> points,
                                            size_t fromSegment,
                                            uint32_t colorRgba,
                                            std::vector<CapsuleInstance>& outInstances) {
    const size_t n = points.size();
    if (n < 2) return;

    const size_t segCount = n - 1;
    if (fromSegment >= segCount) return;
    outInstances.resize(segCount);
    const StrokePoint* src = points.data();
    CapsuleInstance* dst = outInstances.data();

    for (size_t i = fromSegment; i < segCount; ++i) {
        const StrokePoint& pA = src[i];
        const StrokePoint& pB = src[i + 1];
        dst[i].pointA.x  = static_cast<float>(pA.x);
        dst[i].pointA.y  = static_cast<float>(pA.y);
        dst[i].pointB.x  = static_cast<float>(pB.x);
        dst[i].pointB.y  = static_cast<float>(pB.y);
        dst[i].radii.x   = pA.r > 0.0f ? pA.r : 0.0f;
        dst[i].radii.y   = pB.r > 0.0f ? pB.r : 0.0f;
        dst[i].colorRgba = colorRgba;
        dst[i].alphaFade = 1.0f;
    }
}

} // namespace Folio
