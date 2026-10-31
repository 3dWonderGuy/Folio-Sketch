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
 * @struct MeshTessellationParams
 * @brief Live-tunable geometric parameters for GPU triangle-strip and corner-fan tessellation.
 */
struct MeshTessellationParams {
    float cornerAngleThresholdRad = 0.35f;   ///< Turn angle threshold (rad) for radial bevel fan vs continuous miter
    float miterMinClampRatio      = 0.0f;    ///< Minimum inner miter clamp multiplier (0.0 .. 1.0x)
    float miterMaxClampRatio      = 1.25f;   ///< Maximum miter clamp multiplier (1.0 .. 2.5x)
    bool  stationaryEnvelopment   = true;    ///< Merge consecutive points where ds <= |dr|
};

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
    static void SetTessellationParams(const MeshTessellationParams& params) noexcept;
    [[nodiscard]] static const MeshTessellationParams& GetTessellationParams() noexcept;

    /**
     * @brief Builds an indexed triangle mesh for a solid variable-width stroke.
     *
     * @param points Filtered modeled centerline vertices.
     * @param capType Terminal cap style (Round, Square, Chisel).
     * @param color Packed RGBA32 color for vertices.
     * @param outMesh Output mesh buffer (reused across calls to minimize allocations).
     * @param scalePxPerMm Effective screen pixels per millimeter scale factor for 1-pixel exterior anti-aliasing skirt.
     */
    static void BuildSolidMesh(std::span<const StrokePoint> points,
                               CapType capType,
                               uint32_t color,
                               InkMesh& outMesh,
                               float scalePxPerMm = 3.7795f);

    /**
     * @brief Builds an indexed triangle mesh for a styled variable-width stroke (Solid, Dashed, Dotted, DashDot).
     */
    static void BuildStyledMesh(std::span<const StrokePoint> points,
                                CapType capType,
                                StrokePattern pattern,
                                uint32_t color,
                                InkMesh& outMesh,
                                float scalePxPerMm = 3.7795f);
};

} // namespace Folio
