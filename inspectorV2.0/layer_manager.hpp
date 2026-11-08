#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/layer_manager.hpp
 * @brief Unified Dual-Layer Manager (Live Inking Fast-Path + Standard Canvas Document)
 * =========================================================================================
 *
 * Responsibilities:
 *   1. Owns the InputManager and configures device routing.
 *   2. Bypass System (Fast Path): Feeds raw Stylus/Mouse packets directly to Folio::InkEngine
 *      and renders live wet strokes on the unbuffered hardware scanout overlay (<2ms).
 *   3. Standard Canvas (Document Layer): On pen lift (UP), commits finished stroke geometry
 *      into the permanent document model, and triggers standard canvas redraw.
 *   4. Touch Navigation: Translates document canvas when finger drag occurs.
 */

#include <vector>
#include <functional>
#include <memory>
#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"
#include "inspectorV2.0/input_manager.hpp"

namespace Folio::V2 {

struct CanvasState {
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;
    Folio::ColorRgba paperColor = Folio::ColorRgba(245, 245, 248, 255);
};

struct DocumentStroke {
    std::vector<Folio::StrokePoint> points;
    uint32_t color = 0;
    Folio::Backends::CapsuleBrush brush{};
};

class LayerManager {
public:
    LayerManager();
    ~LayerManager();

    // Input Manager Access
    InputManager& GetInputManager() noexcept { return m_inputManager; }
    const InputManager& GetInputManager() const noexcept { return m_inputManager; }

    // Ingest normalized pointer sample
    void ProcessPointer(const PointerPacket& packet);

    // Live Inking (Bypass Fast Path)
    Folio::InkEngine& GetLiveEngine() noexcept { return m_liveEngine; }
    [[nodiscard]] bool IsLiveInkingActive() const noexcept;

    // Document Canvas
    [[nodiscard]] const CanvasState& GetCanvasState() const noexcept { return m_canvasState; }
    CanvasState& GetCanvasState() noexcept { return m_canvasState; }

    [[nodiscard]] const std::vector<DocumentStroke>& GetDocumentStrokes() const noexcept {
        return m_documentStrokes;
    }

    void SetActiveBrush(const Folio::StrokeStyle& style, const Folio::Backends::CapsuleBrush& brush);

    // Callbacks
    std::function<void()> onDocumentDirty; ///< Triggered when strokes commit or canvas pans

    // Render Passes
    void RenderDocumentCanvas(int vpWidth, int vpHeight, const float* surfaceTransformMatrix = nullptr);
    void RenderLiveInking(int vpWidth, int vpHeight, const float* surfaceTransformMatrix = nullptr);

    void ClearDocument();

private:
    InputManager                        m_inputManager;
    Folio::InkEngine                    m_liveEngine;
    Folio::Backends::GlCapsuleRenderer  m_capsuleRenderer;

    CanvasState                         m_canvasState{};
    Folio::StrokeStyle                  m_activeStyle{};
    Folio::Backends::CapsuleBrush       m_activeBrush{};

    std::vector<DocumentStroke>         m_documentStrokes;

    float  m_lastX = 0.0f;
    float  m_lastY = 0.0f;
    float  m_lastPressure = 0.5f;
    bool   m_isStrokeActive = false;
    bool   m_glInitialized = false;

    void EnsureGl();
    void InitInputRouting();
};

} // namespace Folio::V2
