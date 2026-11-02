#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/gpu/ink_mesh.hpp
 * @brief High-Performance GPU Capsule SDF Instance Streaming Pipeline
 * =========================================================================================
 */

#include <cstdint>
#include <vector>
#include <span>
#include "folio_sketch/core/ink_stroke_data.hpp"

namespace Folio {

/**
 * @class CapsuleBuilder
 * @brief High-performance GPU Capsule SDF instance generator for variable-width strokes.
 *
 * Emits continuous segment bounding instances in O(N) time with zero trigonometric
 * or polygon-clipping overhead for analytic fragment shader rendering.
 */
class CapsuleBuilder {
public:
    /**
     * @brief Generates GPU Capsule SDF segment instances in O(N) time with zero trigonometric overhead.
     *
     * @param points Modeled centerline vertices.
     * @param colorRgba Packed 32-bit premultiplied RGBA color.
     * @param outInstances Output vector of CapsuleInstance segments (cleared and populated in O(N)).
     */
    static void BuildCapsuleInstances(std::span<const StrokePoint> points,
                                      uint32_t colorRgba,
                                      std::vector<CapsuleInstance>& outInstances);
};

// Clean alias for backwards compatibility
using InkMeshBuilder = CapsuleBuilder;

} // namespace Folio
