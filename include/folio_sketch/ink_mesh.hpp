#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/ink_mesh.hpp
 * @brief Direct GPU Vertex Definition and Triangle-Strip Mesh Tessellator
 * =========================================================================================
 */

#include <cstdint>
#include <vector>
#include <span>
#include "folio_sketch/ink_stroke_data.hpp"

namespace Folio {

/**
 * @class InkMeshBuilder
 * @brief High-performance GPU mesh builder for variable-width strokes.
 *
 * Generates continuous triangle strips / quads and round endcaps from modeled centerline points.
 * Solves the polygon ribbon topology problem by drawing overlapping convex quads and disks
 * directly via GPU rasterization, bypassing CPU winding-rule polygon clipping.
 */
class InkMeshBuilder {
public:
    /**
     * @brief Builds an indexed triangle mesh for a solid variable-width stroke.
     *
     * @param points Filtered modeled centerline vertices.
     * @param capType Terminal cap style (Round, Square, Chisel).
     * @param color Packed RGBA32 color for vertices.
     * @param outMesh Output mesh buffer (reused across calls to minimize allocations).
     */
    static void BuildSolidMesh(std::span<const StrokePoint> points,
                               CapType capType,
                               uint32_t color,
                               InkMesh& outMesh);
};

} // namespace Folio
