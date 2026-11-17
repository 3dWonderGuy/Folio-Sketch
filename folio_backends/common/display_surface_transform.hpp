#pragma once
/**
 * =========================================================================================
 * @file folio_backends/common/display_surface_transform.hpp
 * @brief Authoritative Hardware Display Surface Transform & Coordinate Conventions
 * =========================================================================================
 *
 * This module is the SINGLE SOURCE OF TRUTH for coordinate mapping across:
 *   1. Logical Screen Space (Touch/Stylus inputs, UI hit-testing, [0, 0] at Top-Left)
 *   2. Unrotated Normalized Device Coordinates (NDC [-1.0, +1.0])
 *   3. Android Hardware DPU Direct Scanout (BufferInfo.transform 4x4 matrix)
 *   4. Physical OpenGL Framebuffer / Scissor Space (Window pixels, [0, 0] at Bottom-Left)
 *
 * -----------------------------------------------------------------------------------------
 * THE ROTATION & COORDINATE PROBLEM:
 * -----------------------------------------------------------------------------------------
 * On mobile devices (e.g. Samsung Galaxy Tab with S-Pen):
 * 1. Physical display panels are manufactured with native portrait scanning order.
 * 2. When an app runs in Landscape, Android's SurfaceFlinger and the Display Processing
 *    Unit (DPU) provide a 4x4 rotation matrix via Jetpack's `BufferInfo.transform`.
 * 3. In landscape, this matrix rotates coordinates by 90° or 270°.
 *    Because a 90° rotation swaps axes:
 *        X_panel = -Y_logical
 *        Y_panel =  X_logical
 *    any sign discrepancy in the unrotated Y coordinate flips the rendered image
 *    HORIZONTALLY across the physical screen!
 *
 * -----------------------------------------------------------------------------------------
 * COORDINATE CONVENTION SPECIFICATION:
 * -----------------------------------------------------------------------------------------
 *
 * A. Desktop OpenGL / Standard Offscreen FBO (CoordinateConvention::DesktopStandard):
 *    - Logical (0, 0) is Top-Left.
 *    - NDC (-1.0, -1.0) is Bottom-Left, (+1.0, +1.0) is Top-Right.
 *    - Formula:
 *        ndcX = (x / W) * 2.0 - 1.0
 *        ndcY = 1.0 - (y / H) * 2.0   <-- INVERTED Y
 *
 * B. Android Hardware Front-Buffer / Vulkan Clip / ImGui Android (CoordinateConvention::AndroidFrontBuffer):
 *    - Logical (0, 0) is Top-Left.
 *    - NDC (-1.0, -1.0) is Top-Left, (+1.0, +1.0) is Bottom-Right (Vulkan / DPU scan order).
 *    - BufferInfo.transform matrix expects this unrotated orientation.
 *    - Formula:
 *        ndcX = (x / W) * 2.0 - 1.0
 *        ndcY = (y / H) * 2.0 - 1.0   <-- NON-INVERTED Y
 *
 * C. Physical OpenGL Scissor Box (glScissor):
 *    - Window pixel (0, 0) is ALWAYS the LOWER-LEFT of the physical framebuffer.
 *    - Formula from transformed NDC (post-uSurfaceTransform):
 *        sx = floor((tx * 0.5 + 0.5) * physicalWidth)
 *        sy = floor((ty * 0.5 + 0.5) * physicalHeight)
 * =========================================================================================
 */

#include <cmath>
#include <algorithm>
#include <array>
#include <span>

namespace Folio::Backends {

/**
 * @brief Coordinate system conventions for mapping logical screen pixels to unrotated NDC.
 */
enum class CoordinateConvention {
    /// Desktop OpenGL / Standard Texture FBO: Top is +1.0, Bottom is -1.0
    DesktopStandard,

    /// Android Hardware Front-Buffer Scanout (GLFrontBufferedRenderer / DPU):
    /// Top is -1.0, Bottom is +1.0 (matches Dear ImGui Android + Vulkan clip space)
    AndroidFrontBuffer
};

/**
 * @brief Detected hardware panel rotation relative to application logical orientation.
 */
enum class PanelRotation {
    Rotation0,   ///< 0 degrees (Panel natural orientation matches logical orientation)
    Rotation90,  ///< 90 degrees clockwise hardware panel scan
    Rotation180, ///< 180 degrees hardware panel scan
    Rotation270  ///< 270 degrees clockwise hardware panel scan
};

/**
 * @brief Structure representing a 2D bounding rectangle in logical screen space.
 */
struct LogicalRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid = false;
};

/**
 * @brief Structure representing an OpenGL physical scissor rectangle in window pixels.
 */
struct PhysicalScissorRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool enabled = false;
};

/**
 * @brief Compile-time identity 4x4 matrix (column-major).
 */
inline constexpr std::array<float, 16> kIdentityMatrix4x4 = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

/**
 * @brief Returns the active platform default coordinate convention.
 */
[[nodiscard]] inline constexpr CoordinateConvention GetPlatformDefaultConvention() noexcept {
#if defined(__ANDROID__)
    return CoordinateConvention::AndroidFrontBuffer;
#else
    return CoordinateConvention::DesktopStandard;
#endif
}

/**
 * @brief Converts a 2D logical screen pixel coordinate to unrotated NDC [-1, 1].
 */
inline void ScreenToUnrotatedNdc(
    float screenX, float screenY,
    float viewportWidth, float viewportHeight,
    CoordinateConvention convention,
    float& outNdcX, float& outNdcY) noexcept
{
    const float invW = (viewportWidth > 0.0f) ? (1.0f / viewportWidth) : 0.0f;
    const float invH = (viewportHeight > 0.0f) ? (1.0f / viewportHeight) : 0.0f;

    outNdcX = screenX * invW * 2.0f - 1.0f;
    if (convention == CoordinateConvention::AndroidFrontBuffer) {
        outNdcY = screenY * invH * 2.0f - 1.0f;
    } else {
        outNdcY = 1.0f - screenY * invH * 2.0f;
    }
}

/**
 * @brief Multiplies an unrotated 2D NDC point by an Android column-major 4x4 matrix.
 *
 * Android BufferInfo.transform is formatted in column-major order:
 *   Index 0..3: Column 0 (x basis)
 *   Index 4..7: Column 1 (y basis)
 *   Index 8..11: Column 2 (z basis)
 *   Index 12..15: Column 3 (translation)
 */
inline void TransformNdc(
    float inNdcX, float inNdcY,
    const float* surfaceTransform16,
    float& outTransformedX, float& outTransformedY) noexcept
{
    if (surfaceTransform16 == nullptr) {
        outTransformedX = inNdcX;
        outTransformedY = inNdcY;
        return;
    }

    const float tx = surfaceTransform16[0] * inNdcX + surfaceTransform16[4] * inNdcY + surfaceTransform16[12];
    const float ty = surfaceTransform16[1] * inNdcX + surfaceTransform16[5] * inNdcY + surfaceTransform16[13];
    const float tw = surfaceTransform16[3] * inNdcX + surfaceTransform16[7] * inNdcY + surfaceTransform16[15];

    if (std::abs(tw - 1.0f) > 1e-5f && std::abs(tw) > 1e-5f) {
        const float invW = 1.0f / tw;
        outTransformedX = tx * invW;
        outTransformedY = ty * invW;
    } else {
        outTransformedX = tx;
        outTransformedY = ty;
    }
}

/**
 * @brief Analyzes a 4x4 column-major surface transform matrix and detects panel rotation.
 */
[[nodiscard]] PanelRotation DetectPanelRotation(const float* surfaceTransform16) noexcept;

/**
 * @brief Transforms a logical screen-space bounding rectangle into a physical OpenGL
 *        scissor rectangle ready for `glScissor(x, y, w, h)`.
 *
 * Correctly accounts for:
 *   - Logical screen top-left origin.
 *   - Android hardware scanout orientation (90°/180°/270° swaps and reflections).
 *   - OpenGL window coordinate lower-left origin.
 *   - Viewport bounding clamps.
 */
[[nodiscard]] PhysicalScissorRect CalculatePhysicalScissor(
    const LogicalRect& logicalRect,
    int viewportWidth, int viewportHeight,
    const float* surfaceTransform16,
    int physicalWidth, int physicalHeight,
    CoordinateConvention convention = GetPlatformDefaultConvention()) noexcept;

/**
 * @brief Generates an orthographic projection matrix (column-major 4x4) matching the specified convention.
 */
void BuildOrthoMatrix(
    float left, float right, float top, float bottom,
    CoordinateConvention convention,
    float outMatrix16[16]) noexcept;

/**
 * @brief Multiplies two column-major 4x4 matrices: outMatrix = matrixA * matrixB.
 */
void MultiplyMatrix4x4(
    const float matrixA[16],
    const float matrixB[16],
    float outMatrix[16]) noexcept;

/**
 * @brief GLSL vertex shader code snippet for unified NDC transformation.
 *
 * Embed this in vertex shaders to ensure 100% mathematical parity with C++.
 */
[[nodiscard]] const char* GetGlslNdcSnippet() noexcept;

} // namespace Folio::Backends
