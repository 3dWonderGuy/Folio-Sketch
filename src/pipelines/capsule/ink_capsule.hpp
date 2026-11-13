#pragma once
/**
 * =========================================================================================
 * @file src/pipelines/capsule/ink_capsule.hpp
 * @brief GPU Capsule & Shape SDF Instance Streaming Pipeline
 * =========================================================================================
 */

#include <cstdint>
#include <vector>
#include <span>
#include <algorithm>
#include <type_traits>
#include "core/ink_stroke_data.hpp"

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
// 3. GPU SHAPE SDF INSTANCE TYPES
// =============================================================================

/**
 * @enum ShapeKind
 * @brief Categorizes geometric primitives rendered via analytic GPU SDF shaders.
 */
enum class ShapeKind : uint32_t {
    Rectangle        = 0, ///< Axis-aligned rectangle
    RoundedRectangle = 1, ///< Rectangle with rounded corners (cornerRadius)
    Circle           = 2, ///< Circle (size.x = diameter, cornerRadius = radius)
    Ellipse          = 3  ///< Axis-aligned ellipse (size.x = 2*rx, size.y = 2*ry)
};

/**
 * @struct GpuShapeInstance
 * @brief Per-shape instance attribute layout streamed to GPU Shape SDF shader.
 */
struct GpuShapeInstance {
    CapsuleVec2 center{};                     ///< Center in world coordinates (mm)
    CapsuleVec2 size{};                       ///< (width, height) in world coordinates (mm)
    float       cornerRadius = 0.0f;          ///< Corner radius for rounded rectangles (mm)
    float       strokeWidth  = 0.0f;          ///< Border width (mm); 0.0f = filled shape without border
    uint32_t    fillColor    = 0x00000000u;   ///< Packed 32-bit premultiplied RGBA fill
    uint32_t    strokeColor  = 0xFF000000u;   ///< Packed 32-bit premultiplied RGBA border/stroke
    ShapeKind   kind         = ShapeKind::Rectangle;
};

// =============================================================================
// 4. BUILDERS (CAPSULE & SHAPE)
// =============================================================================

/**
 * @class ShapeBuilder
 * @brief Helper utility to assemble GpuShapeInstance structs for real-time GPU rendering.
 */
class ShapeBuilder {
public:
    [[nodiscard]] static constexpr GpuShapeInstance MakeRectangle(
        float centerX, float centerY, float width, float height,
        uint32_t fillColor, uint32_t strokeColor = 0, float strokeWidth = 0.0f) noexcept {
        GpuShapeInstance inst{};
        inst.center = {centerX, centerY};
        inst.size = {width, height};
        inst.cornerRadius = 0.0f;
        inst.strokeWidth = strokeWidth;
        inst.fillColor = fillColor;
        inst.strokeColor = strokeColor;
        inst.kind = ShapeKind::Rectangle;
        return inst;
    }

    [[nodiscard]] static constexpr GpuShapeInstance MakeRoundedRectangle(
        float centerX, float centerY, float width, float height, float cornerRadius,
        uint32_t fillColor, uint32_t strokeColor = 0, float strokeWidth = 0.0f) noexcept {
        GpuShapeInstance inst{};
        inst.center = {centerX, centerY};
        inst.size = {width, height};
        inst.cornerRadius = cornerRadius;
        inst.strokeWidth = strokeWidth;
        inst.fillColor = fillColor;
        inst.strokeColor = strokeColor;
        inst.kind = ShapeKind::RoundedRectangle;
        return inst;
    }

    [[nodiscard]] static constexpr GpuShapeInstance MakeCircle(
        float centerX, float centerY, float radius,
        uint32_t fillColor, uint32_t strokeColor = 0, float strokeWidth = 0.0f) noexcept {
        GpuShapeInstance inst{};
        inst.center = {centerX, centerY};
        inst.size = {radius * 2.0f, radius * 2.0f};
        inst.cornerRadius = radius;
        inst.strokeWidth = strokeWidth;
        inst.fillColor = fillColor;
        inst.strokeColor = strokeColor;
        inst.kind = ShapeKind::Circle;
        return inst;
    }

    [[nodiscard]] static constexpr GpuShapeInstance MakeEllipse(
        float centerX, float centerY, float radiusX, float radiusY,
        uint32_t fillColor, uint32_t strokeColor = 0, float strokeWidth = 0.0f) noexcept {
        GpuShapeInstance inst{};
        inst.center = {centerX, centerY};
        inst.size = {radiusX * 2.0f, radiusY * 2.0f};
        inst.cornerRadius = 0.0f;
        inst.strokeWidth = strokeWidth;
        inst.fillColor = fillColor;
        inst.strokeColor = strokeColor;
        inst.kind = ShapeKind::Ellipse;
        return inst;
    }
};

/**
 * @class CapsuleBuilder
 * @brief Generates GPU Capsule SDF instances from centerline points in O(N) time.
 */
class CapsuleBuilder {
public:
    static void BuildCapsuleInstances(std::span<const StrokePoint> points,
                                      uint32_t colorRgba,
                                      std::vector<CapsuleInstance>& outInstances);

    /**
     * @brief Incremental variant: appends only the segments [fromSegment, points.size() - 1).
     *
     * `outInstances` must already hold exactly `fromSegment` valid segment instances (or be empty
     * when fromSegment == 0). Used by the live stroke so each new modeled point costs O(1).
     */
    static void AppendCapsuleInstances(std::span<const StrokePoint> points,
                                       size_t fromSegment,
                                       uint32_t colorRgba,
                                       std::vector<CapsuleInstance>& outInstances);
};

} // namespace Folio
