#include "folio_sketch/ink_mesh.hpp"
#include "ink_generator_internal.hpp"
#include <cmath>
#include <algorithm>

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

    if (points.size() == 1) {
        // Single point: emit circular/nib disk cap
        const auto& p = points.front();
        float r = std::max(0.0f, p.r);
        if (r <= 0.0f) return;

        constexpr int steps = 24;
        outMesh.reserve(steps + 1, steps * 3);

        uint32_t centerIdx = 0;
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p.x),
            .y = static_cast<float>(p.y),
            .edgeDist = 0.0f,
            .radius = r,
            .color = color
        });

        for (int i = 0; i < steps; ++i) {
            float angle = static_cast<float>(2.0 * M_PI * i / steps);
            outMesh.vertices.push_back(InkVertex{
                .x = static_cast<float>(p.x + std::cos(angle) * r),
                .y = static_cast<float>(p.y + std::sin(angle) * r),
                .edgeDist = 1.0f,
                .radius = r,
                .color = color
            });
        }

        for (int i = 0; i < steps; ++i) {
            uint32_t next = static_cast<uint32_t>(1 + (i + 1) % steps);
            outMesh.indices.push_back(centerIdx);
            outMesh.indices.push_back(static_cast<uint32_t>(1 + i));
            outMesh.indices.push_back(next);
        }
        return;
    }

    const size_t N = points.size();
    outMesh.reserve(N * 6, (N - 1) * 12 + N * 24);

    // Lambda to emit a round disk cap at a point
    auto emitRoundCap = [&](const StrokePoint& p, double startAngle, double sweepAngle, int steps = 12) {
        float r = std::max(0.0f, p.r);
        if (r <= 0.0f || std::abs(sweepAngle) < 1e-4) return;

        uint32_t centerIdx = static_cast<uint32_t>(outMesh.vertices.size());
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p.x),
            .y = static_cast<float>(p.y),
            .edgeDist = 0.0f,
            .radius = r,
            .color = color
        });

        uint32_t firstRimIdx = static_cast<uint32_t>(outMesh.vertices.size());
        for (int s = 0; s <= steps; ++s) {
            double a = startAngle + (static_cast<double>(s) / steps) * sweepAngle;
            outMesh.vertices.push_back(InkVertex{
                .x = static_cast<float>(p.x + std::cos(a) * r),
                .y = static_cast<float>(p.y + std::sin(a) * r),
                .edgeDist = 1.0f,
                .radius = r,
                .color = color
            });
        }

        for (int s = 0; s < steps; ++s) {
            outMesh.indices.push_back(centerIdx);
            if (sweepAngle >= 0.0) {
                outMesh.indices.push_back(firstRimIdx + s);
                outMesh.indices.push_back(firstRimIdx + s + 1);
            } else {
                outMesh.indices.push_back(firstRimIdx + s + 1);
                outMesh.indices.push_back(firstRimIdx + s);
            }
        }
    };

    // Emit segment quads (Left rail, Centerline, Right rail)
    for (size_t i = 0; i < N - 1; ++i) {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];

        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double len = std::hypot(dx, dy);
        if (len < 1e-7) continue;

        double nx =  dy / len;
        double ny = -dx / len;

        float r0 = std::max(0.0f, p0.r);
        float r1 = std::max(0.0f, p1.r);

        uint32_t baseIdx = static_cast<uint32_t>(outMesh.vertices.size());

        // Vertices at P0: [0] Left, [1] Center, [2] Right
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p0.x + nx * r0),
            .y = static_cast<float>(p0.y + ny * r0),
            .edgeDist = -1.0f,
            .radius = r0,
            .color = color
        });
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p0.x),
            .y = static_cast<float>(p0.y),
            .edgeDist = 0.0f,
            .radius = r0,
            .color = color
        });
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p0.x - nx * r0),
            .y = static_cast<float>(p0.y - ny * r0),
            .edgeDist = 1.0f,
            .radius = r0,
            .color = color
        });

        // Vertices at P1: [3] Left, [4] Center, [5] Right
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p1.x + nx * r1),
            .y = static_cast<float>(p1.y + ny * r1),
            .edgeDist = -1.0f,
            .radius = r1,
            .color = color
        });
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p1.x),
            .y = static_cast<float>(p1.y),
            .edgeDist = 0.0f,
            .radius = r1,
            .color = color
        });
        outMesh.vertices.push_back(InkVertex{
            .x = static_cast<float>(p1.x - nx * r1),
            .y = static_cast<float>(p1.y - ny * r1),
            .edgeDist = 1.0f,
            .radius = r1,
            .color = color
        });

        // Left quad: (0, 1, 4) and (0, 4, 3)
        outMesh.indices.push_back(baseIdx + 0);
        outMesh.indices.push_back(baseIdx + 1);
        outMesh.indices.push_back(baseIdx + 4);

        outMesh.indices.push_back(baseIdx + 0);
        outMesh.indices.push_back(baseIdx + 4);
        outMesh.indices.push_back(baseIdx + 3);

        // Right quad: (1, 2, 5) and (1, 5, 4)
        outMesh.indices.push_back(baseIdx + 1);
        outMesh.indices.push_back(baseIdx + 2);
        outMesh.indices.push_back(baseIdx + 5);

        outMesh.indices.push_back(baseIdx + 1);
        outMesh.indices.push_back(baseIdx + 5);
        outMesh.indices.push_back(baseIdx + 4);
    }

    // Emit round joints at interior vertices to guarantee solid convex coverage across turns
    if (capType == CapType::Round) {
        for (size_t i = 1; i < N - 1; ++i) {
            emitRoundCap(points[i], 0.0, 2.0 * M_PI, 12);
        }
    }

    // Terminal Caps (Head and Tail)
    if (capType == CapType::Round) {
        // Head cap at P0
        double dx0 = points[1].x - points[0].x;
        double dy0 = points[1].y - points[0].y;
        double a0 = std::atan2(-dy0, -dx0);
        emitRoundCap(points[0], a0 - M_PI * 0.5, M_PI, 12);

        // Tail cap at PN-1
        double dxN = points[N - 1].x - points[N - 2].x;
        double dyN = points[N - 1].y - points[N - 2].y;
        double aN = std::atan2(dyN, dxN);
        emitRoundCap(points[N - 1], aN - M_PI * 0.5, M_PI, 12);
    }
}

} // namespace Folio
