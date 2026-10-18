#include "folio_sketch/ink_mesh.hpp"
#include "ink_generator_internal.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

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

    // 3. Polyline Continuous Ribbon (N >= 2)
    const size_t N = cleanPts.size();
    struct SegmentInfo {
        double len = 0.0;
        double ux = 1.0, uy = 0.0;
        double nx = 0.0, ny = 1.0;
    };
    std::vector<SegmentInfo> segs(N - 1);
    for (size_t i = 0; i < N - 1; ++i) {
        double dx = cleanPts[i + 1].x - cleanPts[i].x;
        double dy = cleanPts[i + 1].y - cleanPts[i].y;
        double l = std::hypot(dx, dy);
        segs[i].len = l;
        if (l > 1e-7) {
            segs[i].ux = dx / l;
            segs[i].uy = dy / l;
            segs[i].nx =  segs[i].uy;
            segs[i].ny = -segs[i].ux;
        } else {
            if (i > 0) {
                segs[i] = segs[i - 1];
            } else {
                segs[i].ux = 1.0; segs[i].uy = 0.0;
                segs[i].nx = 0.0; segs[i].ny = -1.0;
            }
        }
    }

    outMesh.reserve(N * 6, (N - 1) * 12 + 64);

    // Initial cross-section at P0
    float r0 = std::max(0.0f, cleanPts[0].r);
    Point2D offL0, offR0;
    Folio::InkInternal::GetRibbonOffsets(capType, r0, segs[0].nx, segs[0].ny, offL0, offR0);

    uint32_t curLeftIdx   = addVertex(cleanPts[0].x + offL0.x, cleanPts[0].y + offL0.y, -1.0f, r0);
    uint32_t curCenterIdx = addVertex(cleanPts[0].x, cleanPts[0].y, 0.0f, r0);
    uint32_t curRightIdx  = addVertex(cleanPts[0].x + offR0.x, cleanPts[0].y + offR0.y, 1.0f, r0);

    // Head Endcap at P0: smooth round semicircle facing backward (-u0) from curRightIdx to curLeftIdx
    if (capType == CapType::Round) {
        constexpr int headSteps = 16;
        double aStart = std::atan2(offR0.y, offR0.x);
        double aEnd   = std::atan2(offL0.y, offL0.x);
        double sweep  = aEnd - aStart;
        while (sweep < 0.0) sweep += 2.0 * M_PI;
        if (sweep > 2.0 * M_PI) sweep -= 2.0 * M_PI;

        uint32_t prevFan = curRightIdx;
        for (int s = 1; s < headSteps; ++s) {
            double a = aStart + (static_cast<double>(s) / headSteps) * sweep;
            uint32_t arcIdx = addVertex(cleanPts[0].x + std::cos(a) * r0,
                                        cleanPts[0].y + std::sin(a) * r0,
                                        1.0f, r0);
            addTriangle(curCenterIdx, prevFan, arcIdx);
            prevFan = arcIdx;
        }
        addTriangle(curCenterIdx, prevFan, curLeftIdx);
    }

    // Traverse segments and emit continuous quad strips and smooth corner joints
    for (size_t i = 0; i < N - 1; ++i) {
        const size_t nextPtIdx = i + 1;
        float rNext = std::max(0.0f, cleanPts[nextPtIdx].r);

        // Terminal segment ending at P_{N-1}
        if (nextPtIdx == N - 1) {
            Point2D offL_end, offR_end;
            Folio::InkInternal::GetRibbonOffsets(capType, rNext, segs[i].nx, segs[i].ny, offL_end, offR_end);

            uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + offL_end.x, cleanPts[nextPtIdx].y + offL_end.y, -1.0f, rNext);
            uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
            uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x + offR_end.x, cleanPts[nextPtIdx].y + offR_end.y, 1.0f, rNext);

            // Left quad: (curLeft, curCenter, nextCenter) & (curLeft, nextCenter, nextLeft)
            addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
            addTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

            // Right quad: (curCenter, curRight, nextRight) & (curCenter, nextRight, nextCenter)
            addTriangle(curCenterIdx, curRightIdx, nextRightIdx);
            addTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

            // Tail Endcap at P_{N-1}: smooth round semicircle facing forward (+u_{N-2}) from nextLeft to nextRight
            if (capType == CapType::Round) {
                constexpr int tailSteps = 16;
                double aStart = std::atan2(offL_end.y, offL_end.x);
                double aEnd   = std::atan2(offR_end.y, offR_end.x);
                double sweep  = aEnd - aStart;
                while (sweep < 0.0) sweep += 2.0 * M_PI;
                if (sweep > 2.0 * M_PI) sweep -= 2.0 * M_PI;

                uint32_t prevFan = nextLeftIdx;
                for (int s = 1; s < tailSteps; ++s) {
                    double a = aStart + (static_cast<double>(s) / tailSteps) * sweep;
                    uint32_t arcIdx = addVertex(cleanPts[nextPtIdx].x + std::cos(a) * rNext,
                                                cleanPts[nextPtIdx].y + std::sin(a) * rNext,
                                                1.0f, rNext);
                    addTriangle(nextCenterIdx, prevFan, arcIdx);
                    prevFan = arcIdx;
                }
                addTriangle(nextCenterIdx, prevFan, nextRightIdx);
            }
            break;
        }

        // Interior vertex P_{i+1}
        const auto& sIn  = segs[i];
        const auto& sOut = segs[i + 1];

        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
        double turnAngle = std::atan2(std::abs(cross), dot);

        // Gentle turn (< 0.35 rad ~ 20 deg) or polygonal nib: Single continuous bisector vertex on both sides
        if (turnAngle < 0.35 || capType != CapType::Round) {
            double avgNx = sIn.nx + sOut.nx;
            double avgNy = sIn.ny + sOut.ny;
            double avgLen = std::hypot(avgNx, avgNy);
            if (avgLen > 1e-6) {
                avgNx /= avgLen;
                avgNy /= avgLen;
            } else {
                avgNx = sIn.nx;
                avgNy = sIn.ny;
            }

            double miterScale = 1.0 / std::max(0.5, std::cos(turnAngle * 0.5));
            miterScale = std::min(miterScale, 1.25);

            Point2D offL, offR;
            Folio::InkInternal::GetRibbonOffsets(capType, rNext * miterScale, avgNx, avgNy, offL, offR);

            uint32_t nextLeftIdx   = addVertex(cleanPts[nextPtIdx].x + offL.x, cleanPts[nextPtIdx].y + offL.y, -1.0f, rNext);
            uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);
            uint32_t nextRightIdx  = addVertex(cleanPts[nextPtIdx].x + offR.x, cleanPts[nextPtIdx].y + offR.y, 1.0f, rNext);

            // Connect quad
            addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
            addTriangle(curLeftIdx, nextCenterIdx, nextLeftIdx);

            addTriangle(curCenterIdx, curRightIdx, nextRightIdx);
            addTriangle(curCenterIdx, nextRightIdx, nextCenterIdx);

            curLeftIdx   = nextLeftIdx;
            curCenterIdx = nextCenterIdx;
            curRightIdx  = nextRightIdx;
            continue;
        }

        // Sharp turn (turnAngle >= 0.35 rad) with Round cap:
        // In screen coords (+Y down), cross < 0 is CCW (Left is Inner).
        bool leftIsInner = (cross < 0.0) || (cross == 0.0 && dot < 0.0);

        uint32_t nextCenterIdx = addVertex(cleanPts[nextPtIdx].x, cleanPts[nextPtIdx].y, 0.0f, rNext);

        // Inner side: miter-clamped single vertex
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
        if (innerMiterDist > safeLimit) {
            innerMiterDist = rNext; // Clamp safely to disk circumference
        }

        double innerSign = leftIsInner ? 1.0 : -1.0;
        Point2D vInner = {
            cleanPts[nextPtIdx].x + innerSign * avgNx * innerMiterDist,
            cleanPts[nextPtIdx].y + innerSign * avgNy * innerMiterDist
        };
        uint32_t innerIdx = addVertex(vInner.x, vInner.y, leftIsInner ? -1.0f : 1.0f, rNext);

        // Outer side: incoming vertex
        double outerSign = leftIsInner ? -1.0 : 1.0;
        Point2D vOuterIn = {
            cleanPts[nextPtIdx].x + outerSign * sIn.nx * rNext,
            cleanPts[nextPtIdx].y + outerSign * sIn.ny * rNext
        };
        uint32_t outerInIdx = addVertex(vOuterIn.x, vOuterIn.y, leftIsInner ? 1.0f : -1.0f, rNext);

        // Connect incoming quad from cur cross-section
        if (leftIsInner) {
            // Left is Inner
            addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
            addTriangle(curLeftIdx, nextCenterIdx, innerIdx);

            // Right is Outer
            addTriangle(curCenterIdx, curRightIdx, outerInIdx);
            addTriangle(curCenterIdx, outerInIdx, nextCenterIdx);
        } else {
            // Left is Outer
            addTriangle(curLeftIdx, curCenterIdx, nextCenterIdx);
            addTriangle(curLeftIdx, nextCenterIdx, outerInIdx);

            // Right is Inner
            addTriangle(curCenterIdx, curRightIdx, innerIdx);
            addTriangle(curCenterIdx, innerIdx, nextCenterIdx);
        }

        // Smooth Round Outer Fan between incoming and outgoing outer normals
        int fanSteps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 16.0))), 2, 16);
        double aStart = std::atan2(outerSign * sIn.ny,  outerSign * sIn.nx);
        double aEnd   = std::atan2(outerSign * sOut.ny, outerSign * sOut.nx);
        double sweep  = aEnd - aStart;

        // Normal sweep direction along outer boundary
        if (leftIsInner) {
            // Right is outer, sweeps CCW in screen coords (-sweep)
            while (sweep > 0.0) sweep -= 2.0 * M_PI;
        } else {
            // Left is outer, sweeps CW in screen coords (+sweep)
            while (sweep < 0.0) sweep += 2.0 * M_PI;
        }

        uint32_t prevFan = outerInIdx;
        float outerEdgeDist = leftIsInner ? 1.0f : -1.0f;
        for (int s = 1; s <= fanSteps; ++s) {
            double a = aStart + (static_cast<double>(s) / fanSteps) * sweep;
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

        // Setup cross-section for outgoing segment
        curCenterIdx = nextCenterIdx;
        if (leftIsInner) {
            curLeftIdx  = innerIdx;
            curRightIdx = prevFan; // Last outer fan vertex connects to outgoing right quad
        } else {
            curLeftIdx  = prevFan; // Last outer fan vertex connects to outgoing left quad
            curRightIdx = innerIdx;
        }
    }
}

} // namespace Folio
