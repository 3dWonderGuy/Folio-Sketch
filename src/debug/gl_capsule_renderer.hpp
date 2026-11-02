#pragma once
/**
 * =========================================================================================
 * @file src/debug/gl_capsule_renderer.hpp
 * @brief GPU-Driven Analytic Segment Signed Distance Field (Capsule SDF) Instanced Renderer
 * =========================================================================================
 *
 * Replaces CPU ribbon tessellation with O(N) instanced segment bounding boxes and an
 * analytic Euclidean Capsule SDF fragment shader. Eliminates inner offset rail self-intersections,
 * bowtie quads, and inverted triangles on tight curves (R < r) while rendering mathematically
 * exact round joints, endcaps, and 1-pixel screen-space anti-aliasing at any zoom level.
 */

#include <span>
#include <vector>
#include <cstdint>
#include <imgui.h>
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/gpu/ink_mesh.hpp"
#include "debug/raw_sensor_layer.hpp"

namespace Folio::Debug {

/**
 * @struct CapsuleBrush
 * @brief Per-stroke render parameters for brush behaviour testing (pen / pencil / highlighter).
 */
struct CapsuleBrush {
    float opacity      = 1.0f; ///< Whole-stroke opacity applied once at composite (highlighter ~0.35)
    float grain        = 0.0f; ///< Paper-tooth strength 0..1 (pencil)
    float grainScale   = 1.5f; ///< Grain cells per world unit
    bool  unionOverlap = false; ///< true: self-overlap is a set union (no double-darkening)
};

/**
 * @class GlCapsuleRenderer
 * @brief OpenGL 3.3 Core / GLES 3.0 Instanced Capsule SDF Stroke Renderer.
 */
class GlCapsuleRenderer {
public:
    GlCapsuleRenderer() = default;
    ~GlCapsuleRenderer() { Shutdown(); }

    GlCapsuleRenderer(const GlCapsuleRenderer&) = delete;
    GlCapsuleRenderer& operator=(const GlCapsuleRenderer&) = delete;

    /**
     * @brief Compiles the Capsule SDF shaders and initializes the static unit quad VBO and dynamic instance VBO.
     */
    bool Init();

    /**
     * @brief Releases OpenGL shader program, VAO, and VBO resources.
     */
    void Shutdown();

    /**
     * @brief Converts modeled points into CapsuleInstance segments in O(N) time and streams them to the GPU.
     *
     * @param points Modeled stroke centerline points with evaluated radii.
     * @param color Packed 32-bit premultiplied RGBA color.
     */
    void UploadStroke(std::span<const StrokePoint> points, uint32_t color);

    /**
     * @brief Renders the currently uploaded capsule instances using glDrawArraysInstanced.
     */
    void Render(const CanvasTransform& transform, int vpWidth, int vpHeight,
                const CapsuleBrush& brush = {});

    /**
     * @brief Convenience method that uploads and immediately renders a stroke.
     */
    void RenderStroke(std::span<const StrokePoint> points,
                      uint32_t color,
                      const CanvasTransform& transform,
                      int vpWidth,
                      int vpHeight,
                      const CapsuleBrush& brush = {});

    /**
     * @brief Renders optional debug wireframe outlines of the coarse world-space capsule bounding quads.
     */
    void RenderBoundingQuadsWireframe(ImDrawList* drawList, const CanvasTransform& transform) const;

    /**
     * @brief Runs a pure CPU ingestion benchmark on a synthetic N-point stroke and returns elapsed time in ms.
     */
    static double BenchmarkCpuIngestionMs(size_t pointCount = 1000, int iterations = 20);

    [[nodiscard]] const std::vector<CapsuleInstance>& GetInstances() const noexcept { return m_instances; }
    [[nodiscard]] size_t GetInstanceCount() const noexcept { return m_instances.size(); }
    [[nodiscard]] double GetLastCpuIngestionTimeMs() const noexcept { return m_lastCpuIngestionMs; }

private:
    static unsigned int CompileShader(unsigned int type, const char* src);
    void EnsureLayer(int w, int h);
    void DrawInstances(const CanvasTransform& transform, int vpWidth, int vpHeight,
                       const CapsuleBrush& brush) const;

    unsigned int m_compositeProgram = 0;
    unsigned int m_emptyVao   = 0;
    unsigned int m_layerFbo   = 0;
    unsigned int m_layerTex   = 0;
    int          m_layerW     = 0;
    int          m_layerH     = 0;
    int          m_locGrain   = -1;
    int          m_locCompLayer   = -1;
    int          m_locCompOpacity = -1;

    unsigned int m_program     = 0;
    unsigned int m_vao         = 0;
    unsigned int m_quadVbo     = 0;
    unsigned int m_instanceVbo = 0;
    int          m_locTransform = -1;
    int          m_locViewport  = -1;

    std::vector<CapsuleInstance> m_instances;
    double m_lastCpuIngestionMs = 0.0;
};

} // namespace Folio::Debug
