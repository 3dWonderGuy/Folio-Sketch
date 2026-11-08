/**
 * =========================================================================================
 * @file folio_backends/common/display_surface_transform.cpp
 * @brief Implementation of Hardware Display Surface Transform & Coordinate Conventions
 * =========================================================================================
 */

#include "folio_backends/common/display_surface_transform.hpp"

namespace Folio::Backends {

PanelRotation DetectPanelRotation(const float* surfaceTransform16) noexcept {
    if (surfaceTransform16 == nullptr) {
        return PanelRotation::Rotation0;
    }

    // Inspect the upper 2x2 rotation submatrix:
    // M00 = m[0], M10 = m[1] (Column 0)
    // M01 = m[4], M11 = m[5] (Column 1)
    const float m00 = surfaceTransform16[0];
    const float m10 = surfaceTransform16[1];
    const float m01 = surfaceTransform16[4];
    const float m11 = surfaceTransform16[5];

    // Check primary dominant direction:
    if (std::abs(m00) > std::abs(m10)) {
        // Horizontal dominance (0 or 180 degrees)
        return (m00 >= 0.0f) ? PanelRotation::Rotation0 : PanelRotation::Rotation180;
    } else {
        // Vertical dominance (90 or 270 degrees)
        return (m10 >= 0.0f) ? PanelRotation::Rotation90 : PanelRotation::Rotation270;
    }
}

PhysicalScissorRect CalculatePhysicalScissor(
    const LogicalRect& logicalRect,
    int viewportWidth, int viewportHeight,
    const float* surfaceTransform16,
    int physicalWidth, int physicalHeight,
    CoordinateConvention convention) noexcept
{
    PhysicalScissorRect outScissor{};
    if (!logicalRect.valid || logicalRect.w <= 0 || logicalRect.h <= 0 ||
        viewportWidth <= 0 || viewportHeight <= 0 ||
        physicalWidth <= 0 || physicalHeight <= 0)
    {
        outScissor.enabled = false;
        return outScissor;
    }

    // 1. Convert 4 corners of logical rectangle to unrotated NDC
    const float x0 = static_cast<float>(logicalRect.x);
    const float y0 = static_cast<float>(logicalRect.y);
    const float x1 = static_cast<float>(logicalRect.x + logicalRect.w);
    const float y1 = static_cast<float>(logicalRect.y + logicalRect.h);

    const float cornersLogical[4][2] = {
        { x0, y0 },
        { x1, y0 },
        { x1, y1 },
        { x0, y1 }
    };

    float minNdcX =  1e9f, maxNdcX = -1e9f;
    float minNdcY =  1e9f, maxNdcY = -1e9f;

    for (int i = 0; i < 4; ++i) {
        float unrotatedNdcX = 0.0f;
        float unrotatedNdcY = 0.0f;
        ScreenToUnrotatedNdc(cornersLogical[i][0], cornersLogical[i][1],
                             static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
                             convention, unrotatedNdcX, unrotatedNdcY);

        float tx = unrotatedNdcX;
        float ty = unrotatedNdcY;
        TransformNdc(unrotatedNdcX, unrotatedNdcY, surfaceTransform16, tx, ty);

        minNdcX = std::min(minNdcX, tx);
        maxNdcX = std::max(maxNdcX, tx);
        minNdcY = std::min(minNdcY, ty);
        maxNdcY = std::max(maxNdcY, ty);
    }

    // 2. Map transformed NDC [-1, +1] to OpenGL Physical Window Coordinates [0, physicalWidth/Height]
    // In OpenGL, (0, 0) is the LOWER-LEFT of the framebuffer window.
    int sx0 = static_cast<int>(std::floor((minNdcX * 0.5f + 0.5f) * static_cast<float>(physicalWidth)));
    int sy0 = static_cast<int>(std::floor((minNdcY * 0.5f + 0.5f) * static_cast<float>(physicalHeight)));
    int sx1 = static_cast<int>(std::ceil((maxNdcX * 0.5f + 0.5f) * static_cast<float>(physicalWidth)));
    int sy1 = static_cast<int>(std::ceil((maxNdcY * 0.5f + 0.5f) * static_cast<float>(physicalHeight)));

    // 3. Clamp strictly to physical buffer bounds
    sx0 = std::clamp(sx0, 0, physicalWidth);
    sy0 = std::clamp(sy0, 0, physicalHeight);
    sx1 = std::clamp(sx1, 0, physicalWidth);
    sy1 = std::clamp(sy1, 0, physicalHeight);

    if (sx1 > sx0 && sy1 > sy0) {
        outScissor.x = sx0;
        outScissor.y = sy0;
        outScissor.w = sx1 - sx0;
        outScissor.h = sy1 - sy0;
        outScissor.enabled = true;
    } else {
        outScissor.enabled = false;
    }

    return outScissor;
}

void BuildOrthoMatrix(
    float left, float right, float top, float bottom,
    CoordinateConvention convention,
    float outMatrix16[16]) noexcept
{
    const float rml = right - left;
    const float bmt = bottom - top;
    const float invW = (std::abs(rml) > 1e-6f) ? (2.0f / rml) : 0.0f;
    const float invH = (std::abs(bmt) > 1e-6f) ? (2.0f / bmt) : 0.0f;

    // Zero out matrix
    for (int i = 0; i < 16; ++i) outMatrix16[i] = 0.0f;

    // Column 0: X scale & translation
    outMatrix16[0]  = invW;
    outMatrix16[12] = -(right + left) / rml;

    // Column 1: Y scale & translation
    if (convention == CoordinateConvention::AndroidFrontBuffer) {
        // Top maps to -1.0, Bottom maps to +1.0
        outMatrix16[5]  = invH;
        outMatrix16[13] = -(bottom + top) / bmt;
    } else {
        // Desktop OpenGL: Top maps to +1.0, Bottom maps to -1.0
        outMatrix16[5]  = -invH;
        outMatrix16[13] = (bottom + top) / bmt;
    }

    // Column 2 & 3: Z and W identity
    outMatrix16[10] = -1.0f;
    outMatrix16[15] =  1.0f;
}

void MultiplyMatrix4x4(
    const float matrixA[16],
    const float matrixB[16],
    float outMatrix[16]) noexcept
{
    // Out = A * B in column-major order: out[c][r] = sum_k( A[k][r] * B[c][k] )
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += matrixA[k * 4 + r] * matrixB[c * 4 + k];
            }
            outMatrix[c * 4 + r] = sum;
        }
    }
}

const char* GetGlslNdcSnippet() noexcept {
#if defined(__ANDROID__)
    return
        "vec4 ComputeNdc(vec2 screenPos, vec2 viewport, mat4 surfaceTransform) {\n"
        "    vec4 rawNdc = vec4(\n"
        "        (screenPos.x / viewport.x) * 2.0 - 1.0,\n"
        "        (screenPos.y / viewport.y) * 2.0 - 1.0,\n"
        "        0.0,\n"
        "        1.0\n"
        "    );\n"
        "    return surfaceTransform * rawNdc;\n"
        "}\n";
#else
    return
        "vec4 ComputeNdc(vec2 screenPos, vec2 viewport, mat4 surfaceTransform) {\n"
        "    vec4 rawNdc = vec4(\n"
        "        (screenPos.x / viewport.x) * 2.0 - 1.0,\n"
        "        1.0 - (screenPos.y / viewport.y) * 2.0,\n"
        "        0.0,\n"
        "        1.0\n"
        "    );\n"
        "    return surfaceTransform * rawNdc;\n"
        "}\n";
#endif
}

} // namespace Folio::Backends
