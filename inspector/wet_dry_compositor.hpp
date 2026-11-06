#pragma once
/**
 * =========================================================================================
 * @file src/debug/wet_dry_compositor.hpp
 * @brief Two-Phase Layer Compositor (Wet vs Dry) with Unified GPU Capsule SDF
 * =========================================================================================
 *
 * Pillar 2 Architecture:
 * - Active in-flight strokes (Wet) render directly to the front scanout buffer every tick.
 * - Completed strokes commit to a persistent background FBO (Dry) using the identical
 *   Euclidean Capsule SDF fragment shader, guaranteeing 0 visual seams and no opacity pop.
 */

#include <cstdint>
#include <span>
#include <vector>
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "inspector/gl_capsule_renderer.hpp"

namespace Folio::Debug {

/**
 * @class WetDryCompositor
 * @brief Manages decoupled offscreen Dry document FBO caching and active Wet stroke presentation.
 */
class WetDryCompositor {
public:
    WetDryCompositor() = default;
    ~WetDryCompositor() { Shutdown(); }

    WetDryCompositor(const WetDryCompositor&) = delete;
    WetDryCompositor& operator=(const WetDryCompositor&) = delete;

    /**
     * @brief Initializes shader programs and VAO for full-screen dry layer presentation.
     */
    bool Init();

    /**
     * @brief Releases FBO, textures, and shader resources.
     */
    void Shutdown();

    /**
     * @brief Ensures the dry layer FBO texture matches the active viewport resolution.
     */
    void EnsureSize(int width, int height);

    /**
     * @brief Clears the persistent dry layer FBO to transparent (or document background).
     */
    void ClearDry(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f);

    /**
     * @brief Commits a finished stroke into the persistent Dry FBO using GlCapsuleRenderer.
     */
    void CommitStroke(GlCapsuleRenderer& renderer,
                      std::span<const StrokePoint> points,
                      uint32_t color,
                      const CanvasTransform& transform,
                      const CapsuleBrush& brush = {});

    /**
     * @brief Presents the persistent Dry FBO texture to the active target framebuffer.
     * @param vpWidth Active viewport width in pixels.
     * @param vpHeight Active viewport height in pixels.
     * @param debugTintSeams If true, applies a visual color tint to verify wet/dry handoff.
     */
    void PresentDry(int vpWidth, int vpHeight, bool debugTintSeams = false);

    /**
     * @brief Renders the active in-flight wet stroke (and forward prediction) directly over the canvas.
     */
    void RenderWet(GlCapsuleRenderer& renderer,
                   std::span<const StrokePoint> livePoints,
                   std::span<const StrokePoint> predictedPoints,
                   uint32_t color,
                   const CanvasTransform& transform,
                   int vpWidth,
                   int vpHeight,
                   const CapsuleBrush& brush = {},
                   bool debugTintSeams = false);

    [[nodiscard]] unsigned int GetDryTexture() const noexcept { return m_dryTex; }
    [[nodiscard]] unsigned int GetDryFbo() const noexcept { return m_dryFbo; }
    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }
    [[nodiscard]] bool IsSeamDebugEnabled() const noexcept { return m_debugSeamHighlight; }
    void SetSeamDebugEnabled(bool enable) noexcept { m_debugSeamHighlight = enable; }

private:
    static unsigned int CompileShader(unsigned int type, const char* src);

    unsigned int m_dryFbo     = 0;
    unsigned int m_dryTex     = 0;
    int          m_width      = 0;
    int          m_height     = 0;

    unsigned int m_blitProgram = 0;
    unsigned int m_quadVbo     = 0;
    unsigned int m_vao         = 0;
    int          m_locTexture  = -1;
    int          m_locTint     = -1;

    bool         m_debugSeamHighlight = false;
};

} // namespace Folio::Debug
