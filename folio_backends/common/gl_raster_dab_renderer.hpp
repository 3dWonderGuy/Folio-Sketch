#pragma once
/**
 * =========================================================================================
 * @file folio_backends/common/gl_raster_dab_renderer.hpp
 * @brief OpenGL ES 3.0 / Core 3.3 Instanced Procreate-Style Raster Dab & Material Renderer
 * =========================================================================================
 */

#include <span>
#include <cstdint>
#include <cstddef>
#include "core/canvas_material.hpp"
#include "pipelines/raster/ink_raster.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"

namespace Folio::Backends {

class GlRasterDabRenderer {
public:
    GlRasterDabRenderer();
    ~GlRasterDabRenderer();

    GlRasterDabRenderer(const GlRasterDabRenderer&) = delete;
    GlRasterDabRenderer& operator=(const GlRasterDabRenderer&) = delete;

    /**
     * @brief Compiles shaders, creates VAO/VBOs, and prepares persistent streaming memory.
     */
    bool Init();

    /**
     * @brief Releases OpenGL buffers, VAO, and shader program.
     */
    void Shutdown();

    /**
     * @brief Streams RasterDabInstance data into the persistent GPU instance buffer.
     */
    void UploadDabs(std::span<const RasterDabInstance> dabs);

    /**
     * @brief Appends newly generated dabs incrementally to the GPU VBO starting at fromIndex.
     */
    void AppendDabs(std::span<const RasterDabInstance> newDabs, size_t fromIndex);

    /**
     * @brief Renders uploaded dabs modulated by tip shape and physical CanvasMaterial tooth.
     */
    void Render(const CanvasTransform& transform,
                int vpWidth,
                int vpHeight,
                const RasterBrushConfig& brushConfig,
                const CanvasMaterial& material,
                const float* surfaceTransform = nullptr);

    /**
     * @brief Renders a contiguous slice [firstDab, firstDab + dabCount) from the uploaded VBO.
     */
    void RenderRange(size_t firstDab,
                     size_t dabCount,
                     const CanvasTransform& transform,
                     int vpWidth,
                     int vpHeight,
                     const RasterBrushConfig& brushConfig,
                     const CanvasMaterial& material,
                     const float* surfaceTransform = nullptr);

    [[nodiscard]] size_t GetUploadedDabCount() const noexcept { return m_dabCount; }
    [[nodiscard]] bool IsInitialized() const noexcept { return m_program != 0; }

private:
    void EnsureCapacity(size_t requiredCount);

    uint32_t m_program = 0;
    uint32_t m_vao = 0;
    uint32_t m_quadVbo = 0;
    uint32_t m_instanceVbo = 0;

    int m_locTransform        = -1;
    int m_locViewport         = -1;
    int m_locSurfaceTransform = -1;
    int m_locBrushParams      = -1; // (tipShape, aspectRatio, unused, unused)
    int m_locMaterialParams   = -1; // (pattern, toothDepth, toothScaleMm, absorbency)

    size_t m_vboCapacity = 0;
    size_t m_dabCount    = 0;
};

} // namespace Folio::Backends
