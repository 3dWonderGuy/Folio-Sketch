#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/gpu/ink_mesh.hpp
 * @brief GPU Mesh & Capsule SDF Instance Pipelines
 * =========================================================================================
 */

#include <cstdint>
#include <vector>
#include <span>
#include <algorithm>
#include "folio_sketch/core/ink_stroke_data.hpp"

namespace Folio {

// =============================================================================
// 1. GPU COLOR PACKING
// =============================================================================

/**
 * @brief Packs ColorRgba into 32-bit R8G8B8A8 (ABGR little-endian).
 */
[[nodiscard]] constexpr uint32_t PackGpuColor(ColorRgba c, float opacity = 1.0f) noexcept {
    float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
    uint32_t a = static_cast<uint32_t>(static_cast<float>(c.a) * clampedOpacity + 0.5f);
    return (a << 24) | (static_cast<uint32_t>(c.b) << 16) | (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.r);
}

/**
 * @brief Packs ColorRgba into 32-bit premultiplied R8G8B8A8 for GPU Capsule SDF rendering.
 */
[[nodiscard]] constexpr uint32_t PackPremultipliedGpuColor(ColorRgba c, float opacity = 1.0f) noexcept {
    float alphaNorm = (static_cast<float>(c.a) / 255.0f) * std::clamp(opacity, 0.0f, 1.0f);
    uint32_t r = static_cast<uint32_t>(static_cast<float>(c.r) * alphaNorm + 0.5f);
    uint32_t g = static_cast<uint32_t>(static_cast<float>(c.g) * alphaNorm + 0.5f);
    uint32_t b = static_cast<uint32_t>(static_cast<float>(c.b) * alphaNorm + 0.5f);
    uint32_t a = static_cast<uint32_t>(alphaNorm * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// =============================================================================
// 2. GPU CAPSULE SDF INSTANCE TYPES
// =============================================================================

/**
 * @struct CapsuleVec2
 * @brief 2D float coordinate pair for GPU instanced attributes.
 */
struct CapsuleVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * @struct CapsuleInstance
 * @brief Per-instance attribute layout streamed to GPU Capsule SDF shader.
 */
struct CapsuleInstance {
    CapsuleVec2 pointA{};                ///< Segment start in world coordinates (mm)
    CapsuleVec2 pointB{};                ///< Segment end in world coordinates (mm)
    CapsuleVec2 radii{};                 ///< (radiusA, radiusB) for smooth tapering
    uint32_t    colorRgba = 0xFF000000u; ///< Packed 32-bit premultiplied RGBA
};

// =============================================================================
// 3. LEGACY GPU TRIANGLE MESH TYPES
// =============================================================================

/**
 * @struct InkVertex
 * @brief Vertex format for indexed triangle mesh rendering.
 */
struct InkVertex {
    float    x = 0.0f;              ///< World X (mm)
    float    y = 0.0f;              ///< World Y (mm)
    float    edgeDist = 0.0f;       ///< [-1.0, +1.0] signed distance to edge for fragment AA
    float    radius = 0.0f;         ///< Stroke radius at vertex (mm)
    uint32_t color = 0xFF000000u;   ///< Packed RGBA8 color
};

/**
 * @struct InkMesh
 * @brief Vertex and index buffers for GPU triangle mesh rendering.
 */
struct InkMesh {
    std::vector<InkVertex> vertices;
    std::vector<uint32_t>  indices;
    uint32_t coreIndexCount = 0;

    void clear() noexcept {
        vertices.clear();
        indices.clear();
        coreIndexCount = 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return vertices.empty() || indices.empty();
    }
};

// =============================================================================
// 4. CAPSULE BUILDER
// =============================================================================

/**
 * @class CapsuleBuilder
 * @brief Generates GPU Capsule SDF instances from centerline points in O(N) time.
 */
class CapsuleBuilder {
public:
    static void BuildCapsuleInstances(std::span<const StrokePoint> points,
                                      uint32_t colorRgba,
                                      std::vector<CapsuleInstance>& outInstances);
};

using InkMeshBuilder = CapsuleBuilder;

} // namespace Folio
