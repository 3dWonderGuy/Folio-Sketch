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
#include <deque>
#include <chrono>
#include <functional>
#include <memory>
#include <imgui.h>
#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"
#include "inspectorV2.0/input_manager.hpp"
#include "inspectorV2.0/ui/inspector_gui.hpp"

namespace Folio::V2 {

struct CanvasState {
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;
    Folio::ColorRgba paperColor = Folio::ColorRgba(23, 26, 31, 255); // Dark Charcoal (#171A1F)
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

    [[nodiscard]] InspectorGui& GetInspectorGui() noexcept { return m_inspectorGui; }
    [[nodiscard]] const InspectorGui& GetInspectorGui() const noexcept { return m_inspectorGui; }

    // Input Manager Access
    InputManager& GetInputManager() noexcept { return m_inputManager; }
    const InputManager& GetInputManager() const noexcept { return m_inputManager; }

    // Ingest normalized pointer sample
    void ProcessPointer(const PointerPacket& packet);

    // Live Inking (Bypass Fast Path)
    Folio::InkEngine& GetLiveEngine() noexcept { return m_liveEngine; }
    [[nodiscard]] bool IsLiveInkingActive() const noexcept;

    // Active Stroke Style
    [[nodiscard]] Folio::StrokeStyle& GetActiveStyle() noexcept { return m_activeStyle; }
    [[nodiscard]] const Folio::StrokeStyle& GetActiveStyle() const noexcept { return m_activeStyle; }
    [[nodiscard]] Folio::Backends::CapsuleBrush& GetActiveBrush() noexcept { return m_activeBrush; }
    [[nodiscard]] const Folio::Backends::CapsuleBrush& GetActiveBrush() const noexcept { return m_activeBrush; }

    // Last Stroke Raw Samples and Debug Points
    [[nodiscard]] const std::vector<Folio::RawInputSample>& GetLastRawSamples() const noexcept {
        return m_lastRawSamples;
    }
    [[nodiscard]] std::vector<Folio::RawInputSample>& GetMutableRawSamples() noexcept {
        return m_lastRawSamples;
    }
    [[nodiscard]] const std::vector<Folio::StrokePoint>& GetLastFinishedPoints() const noexcept {
        return m_lastFinishedPoints;
    }
    [[nodiscard]] const std::vector<Folio::StrokePoint>& GetLastRawModeledPoints() const noexcept {
        return m_lastRawModeledPoints;
    }
    void SetLastFinishedPoints(std::vector<Folio::StrokePoint> points);

    // Testbed vs Multi-Stroke Document Mode
    void SetTestbedMode(bool testbed) noexcept { m_isTestbedMode = testbed; }
    [[nodiscard]] bool IsTestbedMode() const noexcept { return m_isTestbedMode; }

    // Document Canvas
    [[nodiscard]] const CanvasState& GetCanvasState() const noexcept { return m_canvasState; }
    CanvasState& GetCanvasState() noexcept { return m_canvasState; }

    [[nodiscard]] const std::vector<DocumentStroke>& GetDocumentStrokes() const noexcept {
        return m_documentStrokes;
    }
    void AddDocumentStroke(DocumentStroke stroke);

    void SetActiveBrush(const Folio::StrokeStyle& style, const Folio::Backends::CapsuleBrush& brush);

    // Callbacks
    std::function<void()> onDocumentDirty; ///< Triggered when strokes commit or canvas pans

    // Render Passes
    void RenderDocumentCanvas(int vpWidth, int vpHeight, const float* surfaceTransformMatrix = nullptr, bool renderStrokes = true);
    void RenderLiveInking(int vpWidth, int vpHeight, const float* surfaceTransformMatrix = nullptr);

    // Diagnostics & Modeler Replay
    void ReplayRawStroke();
    void InjectTestScribble();
    void LoadRepro114();

    // Drain any delayed pointer packets whose artificial latency time has elapsed
    bool FlushDelayedPackets();

    // UI Overlay (Dear ImGui HUD)
    void RenderImGui(int vpWidth, int vpHeight, const float* surfaceTransformMatrix = nullptr, bool isSdlHost = false);
    bool HandleTouchForImGui(float x, float y, InputPhase phase);
    void ShutdownImGui();

    void ClearDocument();

    // Stylus Telemetry struct for HUD overlay
    struct StylusTelemetry {
        float screenX = 0.0f;
        float screenY = 0.0f;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float pressure = 0.0f;
        float tiltX = 0.0f;
        float tiltY = 0.0f;
        double timestampSec = 0.0;
        float sampleRateHz = 0.0f;
        float inputLatencyMs = 0.0f;
        bool isHoverOrActive = false;
    };

    [[nodiscard]] const StylusTelemetry& GetStylusTelemetry() const noexcept { return m_telemetry; }

private:
    InputManager                        m_inputManager;
    Folio::InkEngine                    m_liveEngine;
    Folio::Backends::GlCapsuleRenderer  m_capsuleRenderer;

    CanvasState                         m_canvasState{};
    Folio::StrokeStyle                  m_activeStyle{};
    Folio::Backends::CapsuleBrush       m_activeBrush{};

    std::vector<DocumentStroke>         m_documentStrokes;
    std::vector<Folio::RawInputSample>  m_lastRawSamples;
    std::vector<Folio::StrokePoint>     m_lastRawModeledPoints;
    std::vector<Folio::StrokePoint>     m_lastFinishedPoints;

    StylusTelemetry                     m_telemetry{};
    double                              m_lastPacketTimeSec = 0.0;

    float  m_lastX = 0.0f;
    float  m_lastY = 0.0f;
    float  m_lastPressure = 0.5f;
    bool   m_isStrokeActive = false;
    bool   m_isTestbedMode = true;
    bool   m_glInitialized = false;
    bool   m_imguiInitialized = false;
    bool   m_touchCapturedByImGui = false;
    InspectorGui m_inspectorGui{};

    struct DelayedPacket {
        PointerPacket packet;
        std::chrono::steady_clock::time_point releaseTime;
    };
    std::deque<DelayedPacket>           m_delayedPackets;

    void EnsureGl();
    void EnsureImGui(int vpWidth, int vpHeight, bool isSdlHost = false);
    void InitInputRouting();
};

} // namespace Folio::V2
