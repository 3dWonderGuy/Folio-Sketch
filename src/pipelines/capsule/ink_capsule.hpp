#pragma once
/**
 * =========================================================================================
 * @file src/pipelines/capsule/ink_capsule.hpp
 * @brief GPU Capsule SDF Instance Streaming Pipeline
 * =========================================================================================
 *
 * ZERO-COPY MEMORY OWNERSHIP & STREAMING FLOW:
 * 
 * 1. BUFFER OWNERSHIP:
 *    InkEngine owns `m_liveCapsules` (a pre-reserved std::vector<CapsuleInstance> in RAM).
 * 
 * 2. IN-PLACE POPULATION:
 *    InkEngine passes its buffer by reference into CapsuleBuilder::BuildCapsuleInstances.
 *    CapsuleBuilder performs zero heap reallocations during live inking—it edits/appends
 *    the 32-byte POD CapsuleInstance structs directly in place inside that owned buffer.
 * 
 * 3. ZERO-COPY GPU STREAMING:
 *    The active rendering backend (folio_backends/) calls InkEngine::LiveCapsuleSpan(),
 *    which returns a std::span<const CapsuleInstance> pointing directly to that RAM buffer.
 *    The backend streams that memory slice straight into the GPU staging/vertex buffer
 *    (e.g., via glBufferSubData or Vulkan/Metal mapped memory) for single-draw-call instanced rendering.
 */

#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>
#include "core/ink_stroke_data.hpp"
#include "engine/brush/brush_recipe.hpp"

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
    CapsuleVec2 pointA{};                   ///< Segment start in world coordinates (mm)
    CapsuleVec2 pointB{};                   ///< Segment end in world coordinates (mm)
    CapsuleVec2 radii{};                    ///< (radiusA, radiusB) for smooth tapering
    uint32_t    colorRgba    = 0xFF000000u; ///< Packed 32-bit premultiplied RGBA at pointA (start)
    uint32_t    colorRgbaEnd = 0xFF000000u; ///< Packed 32-bit premultiplied RGBA at pointB (end) for smooth gradient & union
};
static_assert(sizeof(CapsuleInstance) == 32, "CapsuleInstance must stay a tightly packed 32-byte POD"); 
static_assert(std::is_trivially_copyable_v<CapsuleInstance>, "CapsuleInstance must be a plain POD");

// =============================================================================
// 3. BUILDER
// =============================================================================

/**
 * @class CapsuleBuilder
 * @brief Generates GPU Capsule SDF instances from centerline points in O(N) time.
 */
class CapsuleBuilder {
public:

    /**
     * @brief Generates GPU Capsule SDF instances from centerline points in O(N) time.
     * @param points Modeled centerline points.
     * @param colorRgba Packed 32-bit premultiplied RGBA color.
     * @param outInstances Destination vector of 32-byte GPU capsule instances.
     * @param fromSegment Starting segment index for incremental live building (default 0 = full rebuild).
     */
    static void BuildCapsuleInstances(std::span<const StrokePoint> points,
                                      uint32_t colorRgba,
                                      std::vector<CapsuleInstance>& outInstances,
                                      size_t fromSegment = 0);

    /**
     * @brief Synthesizes a multi-coat BrushRecipe into an instanced GPU capsule stream.
     */
    static void BuildRecipeInstances(std::span<const StrokePoint> points,
                                     ColorRgba baseColor,
                                     float baseWidth,
                                     const BrushRecipe& recipe,
                                     double currentTimeSec,
                                     std::vector<CapsuleInstance>& outInstances,
                                     double strokeEndTimeSec = 0.0);
};

} // namespace Folio
