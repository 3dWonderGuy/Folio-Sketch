/**
 * =========================================================================================
 * @file src/mesh/ink_mesh_patterns.cpp
 * @brief Styled Stroke Pattern Mesh Builder (Solid, Dotted, Dashed, DashDot)
 * =========================================================================================
 */

#include "folio_sketch/ink_mesh.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Folio {

void InkMeshBuilder::BuildStyledMesh(std::span<const StrokePoint> points,
                                     CapType capType,
                                     StrokePattern pattern,
                                     uint32_t color,
                                     InkMesh& outMesh,
                                     float scalePxPerMm) {
    outMesh.clear();
    if (points.empty()) return;

    if (pattern == StrokePattern::Solid || points.size() == 1) {
        BuildSolidMesh(points, capType, color, outMesh, scalePxPerMm);
        return;
    }

    const size_t N = points.size();
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        BuildSolidMesh(points.subspan(0, 1), capType, color, outMesh, scalePxPerMm);
        return;
    }

    float avgR = 0.0f;
    for (const auto& pt : points) avgR += pt.r;
    avgR /= static_cast<float>(N);

    auto sampleAt = [&](double targetDist) -> StrokePoint {
        targetDist = std::clamp(targetDist, 0.0, totalLen);
        auto it = std::upper_bound(cumDist.begin(), cumDist.end(), targetDist);
        size_t idx = (it == cumDist.begin()) ? 0 : std::distance(cumDist.begin(), it) - 1;
        if (idx >= N - 1) return points.back();

        double segStart = cumDist[idx];
        double segEnd   = cumDist[idx + 1];
        double segSpan  = segEnd - segStart;
        double t = (segSpan > 1e-7) ? (targetDist - segStart) / segSpan : 0.0;
        t = std::clamp(t, 0.0, 1.0);

        return StrokePoint(
            points[idx].x + t * (points[idx + 1].x - points[idx].x),
            points[idx].y + t * (points[idx + 1].y - points[idx].y),
            points[idx].r + static_cast<float>(t) * (points[idx + 1].r - points[idx].r),
            points[idx].pressure + static_cast<float>(t) * (points[idx + 1].pressure - points[idx].pressure),
            points[idx].tiltX + static_cast<float>(t) * (points[idx + 1].tiltX - points[idx].tiltX),
            points[idx].tiltY + static_cast<float>(t) * (points[idx + 1].tiltY - points[idx].tiltY)
        );
    };

    auto appendSubMesh = [&](const InkMesh& subMesh) {
        if (subMesh.empty()) return;
        uint32_t baseVtx = static_cast<uint32_t>(outMesh.vertices.size());
        outMesh.vertices.insert(outMesh.vertices.end(), subMesh.vertices.begin(), subMesh.vertices.end());

        // Separate core indices and skirt indices to preserve dual-pass partitioning
        std::vector<uint32_t> subCoreIndices;
        std::vector<uint32_t> subSkirtIndices;
        subCoreIndices.reserve(subMesh.coreIndexCount);
        subSkirtIndices.reserve(subMesh.indices.size() - subMesh.coreIndexCount);

        for (size_t i = 0; i < subMesh.coreIndexCount; ++i) {
            subCoreIndices.push_back(baseVtx + subMesh.indices[i]);
        }
        for (size_t i = subMesh.coreIndexCount; i < subMesh.indices.size(); ++i) {
            subSkirtIndices.push_back(baseVtx + subMesh.indices[i]);
        }

        // Insert new core indices before existing skirt indices
        outMesh.indices.insert(outMesh.indices.begin() + outMesh.coreIndexCount, subCoreIndices.begin(), subCoreIndices.end());
        outMesh.coreIndexCount += static_cast<uint32_t>(subCoreIndices.size());
        outMesh.indices.insert(outMesh.indices.end(), subSkirtIndices.begin(), subSkirtIndices.end());
    };

    if (pattern == StrokePattern::Dotted) {
        double curDist = 0.0;
        while (curDist <= totalLen) {
            StrokePoint dotPt = sampleAt(curDist);
            InkMesh dotMesh;
            BuildSolidMesh(std::span<const StrokePoint>(&dotPt, 1), CapType::Round, color, dotMesh, scalePxPerMm);
            appendSubMesh(dotMesh);

            double spacing = std::max(0.6, static_cast<double>(dotPt.r) * 4.4);
            curDist += spacing;
        }
        return;
    }

    if (pattern == StrokePattern::Dashed) {
        double dashLen  = std::max(1.8, static_cast<double>(avgR) * 8.0);
        double gapLen   = std::max(1.2, static_cast<double>(avgR) * 4.5);
        double cycleLen = dashLen + gapLen;

        double curDist = 0.0;
        while (curDist < totalLen) {
            double dashStart = curDist;
            double dashEnd   = std::min(totalLen, curDist + dashLen);

            std::vector<StrokePoint> dashPts;
            dashPts.push_back(sampleAt(dashStart));

            auto it = std::upper_bound(cumDist.begin(), cumDist.end(), dashStart);
            while (it != cumDist.end() && *it < dashEnd) {
                dashPts.push_back(points[std::distance(cumDist.begin(), it)]);
                ++it;
            }
            dashPts.push_back(sampleAt(dashEnd));

            InkMesh dashMesh;
            BuildSolidMesh(dashPts, CapType::Round, color, dashMesh, scalePxPerMm);
            appendSubMesh(dashMesh);

            curDist += cycleLen;
        }
        return;
    }

    if (pattern == StrokePattern::DashDot) {
        double dashLen  = std::max(2.0, static_cast<double>(avgR) * 9.0);
        double gap1     = std::max(1.0, static_cast<double>(avgR) * 3.5);
        double dotGap   = std::max(1.0, static_cast<double>(avgR) * 3.5);
        double cycleLen = dashLen + gap1 + dotGap;

        double curDist = 0.0;
        while (curDist < totalLen) {
            // 1. Dash
            double dashStart = curDist;
            double dashEnd   = std::min(totalLen, curDist + dashLen);

            std::vector<StrokePoint> dashPts;
            dashPts.push_back(sampleAt(dashStart));

            auto it = std::upper_bound(cumDist.begin(), cumDist.end(), dashStart);
            while (it != cumDist.end() && *it < dashEnd) {
                dashPts.push_back(points[std::distance(cumDist.begin(), it)]);
                ++it;
            }
            dashPts.push_back(sampleAt(dashEnd));

            InkMesh dashMesh;
            BuildSolidMesh(dashPts, CapType::Round, color, dashMesh, scalePxPerMm);
            appendSubMesh(dashMesh);

            // 2. Dot
            double dotDist = curDist + dashLen + gap1;
            if (dotDist <= totalLen) {
                StrokePoint dotPt = sampleAt(dotDist);
                InkMesh dotMesh;
                BuildSolidMesh(std::span<const StrokePoint>(&dotPt, 1), CapType::Round, color, dotMesh, scalePxPerMm);
                appendSubMesh(dotMesh);
            }

            curDist += cycleLen;
        }
        return;
    }

    // Default fallback
    BuildSolidMesh(points, capType, color, outMesh, scalePxPerMm);
}

} // namespace Folio
