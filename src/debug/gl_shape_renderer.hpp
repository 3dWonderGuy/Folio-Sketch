#pragma once
/**
 * =========================================================================================
 * @file src/debug/gl_shape_renderer.hpp
 * @brief GPU-Driven Analytic Signed Distance Field (SDF) Instanced Shape Renderer
 * =========================================================================================
 *
 * Diagnostic reference renderer for Dear ImGui + OpenGL 3.3 Core / GLES 3.0.
 * Renders geometric shapes (rectangles, rounded rectangles, circles, ellipses) directly
 * on the GPU via instanced bounding quads and analytic 2D SDF fragment evaluations.
 * Provides sub-pixel screen-space anti-aliasing, rounded corners, solid fills, and borders
 * in a single pass with zero CPU triangle tessellation.
 */

#include <vector>
#include <span>
#include <cstdint>
#include "folio_sketch/gpu/ink_mesh.hpp"
#include "debug/raw_sensor_layer.hpp"

namespace Folio {

/**
 * @class GlShapeRenderer
 * @brief OpenGL 3.3 Core / GLES 3.0 Instanced Shape SDF Renderer.
 */
class GlShapeRenderer {
public:
    GlShapeRenderer() = default;
    ~GlShapeRenderer() { Shutdown(); }

    GlShapeRenderer(const GlShapeRenderer&) = delete;
    GlShapeRenderer& operator=(const GlShapeRenderer&) = delete;

    /**
     * @brief Compiles the Shape SDF shaders and initializes instance buffer.
     */
    bool Init();

    /**
     * @brief Releases shader program, VAO, and VBO resources.
     */
    void Shutdown();

    /**
     * @brief Uploads shape instances to dynamic GPU instance VBO.
     */
    void UploadShapes(std::span<const GpuShapeInstance> shapes);

    /**
     * @brief Renders the currently uploaded shapes using glDrawArraysInstanced.
     */
    void Render(const Debug::CanvasTransform& transform, int vpWidth, int vpHeight);

    [[nodiscard]] size_t GetShapeCount() const noexcept { return m_shapes.size(); }

private:
    static unsigned int CompileShader(unsigned int type, const char* src);

    unsigned int m_program     = 0;
    unsigned int m_vao         = 0;
    unsigned int m_quadVbo     = 0;
    unsigned int m_instanceVbo = 0;
    int          m_locTransform = -1;
    int          m_locViewport  = -1;

    std::vector<GpuShapeInstance> m_shapes;
};

namespace Debug {
    using GlShapeRenderer = Folio::GlShapeRenderer;
}

} // namespace Folio
