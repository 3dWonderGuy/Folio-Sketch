#pragma once
/**
 * =========================================================================================
 * @file folio_backends/common/gl_capsule_renderer.hpp
 * @brief Standalone OpenGL ES 3.0 / Core 3.3 Analytic Capsule SDF Instanced Renderer
 * =========================================================================================
 *
 * Optional hardware rendering plug for Folio-Sketch.
 * Translates CapsuleInstance streams into O(N) GPU-instanced Euclidean Signed Distance Field
 * quads with sub-pixel screen-space antialiasing, dynamic procedural paper tooth (grain),
 * and persistent streaming VBO memory reuse.
 */

#include <span>
#include <vector>
#include <cstdint>
#include <cstddef>
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/gpu/ink_mesh.hpp"

#if __has_include(<imgui.h>)
#include <imgui.h>
#define FOLIO_BACKENDS_HAS_IMGUI 1
#endif

namespace Folio::Backends {

/**
 * @struct CanvasTransform
 * @brief 2D canvas pan & zoom mapping world millimetres to viewport pixels.
 */
struct CanvasTransform {
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;

    constexpr CanvasTransform() = default;
    constexpr CanvasTransform(float px, float py, float z) : panX(px), panY(py), zoom(z) {}

#if defined(FOLIO_BACKENDS_HAS_IMGUI)
    [[nodiscard]] ImVec2 WorldToScreen(float wx, float wy) const noexcept {
        return ImVec2(wx * zoom + panX, wy * zoom + panY);
    }
    [[nodiscard]] ImVec2 ScreenToWorld(float sx, float sy) const noexcept {
        float invZ = (zoom > 1e-5f) ? (1.0f / zoom) : 1.0f;
        return ImVec2((sx - panX) * invZ, (sy - panY) * invZ);
    }
#endif
};

/**
 * @struct CapsuleBrush
 * @brief Per-stroke visual parameters (pencil paper-grain tooth, opacity, blend union).
 */
struct CapsuleBrush {
    float opacity      = 1.0f; ///< Composite opacity (e.g. 0.35 for highlighter)
    float grain        = 0.0f; ///< Procedural paper tooth strength [0.0, 1.0]
    float grainScale   = 1.5f; ///< Grain frequency in cells per millimetre
    bool  unionOverlap = false; ///< true: set-union coverage via offscreen MAX layer
};

/**
 * @class GlCapsuleRenderer
 * @brief Standalone GLES 3.0 / GL 3.3 instanced capsule stroke renderer with persistent VBO streaming.
 */
class GlCapsuleRenderer {
public:
    GlCapsuleRenderer();
    ~GlCapsuleRenderer();

    GlCapsuleRenderer(const GlCapsuleRenderer&) = delete;
    GlCapsuleRenderer& operator=(const GlCapsuleRenderer&) = delete;

    /**
     * @brief Compiles GLES 3.0 shaders, configures vertex array layouts, and allocates persistent dynamic VBO.
     */
    bool Init();

    /**
     * @brief Releases GPU shaders, buffers, and framebuffers.
     */
    void Shutdown();

    /**
     * @brief Builds capsule instances from stroke points and uploads them via glBufferSubData.
     */
    void UploadStroke(std::span<const StrokePoint> points, uint32_t color);

    /**
     * @brief Uploads an entire batch of pre-computed CapsuleInstance primitives using streaming glBufferSubData.
     */
    void UploadInstances(std::span<const CapsuleInstance> instances);

    /**
     * @brief Incrementally appends new capsule instances to the active persistent VBO at fromIndex.
     *        Zero per-frame buffer reallocations.
     */
    void AppendInstances(std::span<const CapsuleInstance> newInstances, size_t fromIndex);

    /**
     * @brief Renders the currently uploaded capsule instances.
     * @param transform 2D pan/zoom viewport transform.
     * @param vpWidth Viewport pixel width.
     * @param vpHeight Viewport pixel height.
     * @param brush Brush parameters (grain, unionOverlap, opacity).
     * @param singlePassWet When true, bypasses offscreen layer FBO allocations/clears for minimum latency live wet strokes.
     */
    void Render(const CanvasTransform& transform, int vpWidth, int vpHeight,
                const CapsuleBrush& brush = {}, bool singlePassWet = false);

    /**
     * @brief Convenience helper: uploads points and immediately renders them.
     */
    void RenderStroke(std::span<const StrokePoint> points,
                      uint32_t color,
                      const CanvasTransform& transform,
                      int vpWidth,
                      int vpHeight,
                      const CapsuleBrush& brush = {},
                      bool singlePassWet = false);

#if defined(FOLIO_BACKENDS_HAS_IMGUI)
    /**
     * @brief Renders debug wireframe bounding quads of the active capsule segments.
     */
    void RenderBoundingQuadsWireframe(ImDrawList* drawList, const CanvasTransform& transform) const;
#endif

    /**
     * @brief Microbenchmark for CPU CapsuleInstance generation throughput.
     */
    static double BenchmarkCpuIngestionMs(size_t pointCount = 1000, int iterations = 20);

    [[nodiscard]] const std::vector<CapsuleInstance>& GetInstances() const noexcept { return m_instances; }
    [[nodiscard]] size_t GetInstanceCount() const noexcept { return m_instanceCount; }
    [[nodiscard]] double GetLastCpuIngestionTimeMs() const noexcept { return m_lastCpuIngestionMs; }

private:
    static unsigned int CompileShader(unsigned int type, const char* src);
    void EnsureCapacity(size_t requiredCount);
    void EnsureLayer(int w, int h);
    void DrawInstances(const CanvasTransform& transform, int vpWidth, int vpHeight,
                       const CapsuleBrush& brush) const;

    // Shader programs & locations
    unsigned int m_program         = 0;
    unsigned int m_vao             = 0;
    unsigned int m_quadVbo         = 0;
    unsigned int m_instanceVbo     = 0;
    size_t       m_vboCapacity     = 0;
    size_t       m_instanceCount   = 0;
    int          m_locTransform    = -1;
    int          m_locViewport     = -1;
    int          m_locGrain        = -1;

    // Stroke-layer FBO (for unionOverlap multi-pass composite)
    unsigned int m_compositeProgram = 0;
    unsigned int m_compositeQuadVbo = 0;
    unsigned int m_compositeVao     = 0;
    unsigned int m_layerFbo         = 0;
    unsigned int m_layerTex         = 0;
    int          m_layerW           = 0;
    int          m_layerH           = 0;
    int          m_locCompLayer     = -1;
    int          m_locCompOpacity   = -1;

    std::vector<CapsuleInstance> m_instances;
    double m_lastCpuIngestionMs = 0.0;
};

} // namespace Folio::Backends
