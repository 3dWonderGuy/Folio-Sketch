#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/ui_manager.hpp
 * @brief Embedded Dear ImGui UI Manager for MultiBuffered Sublayer Rendering
 * =========================================================================================
 *
 * Renders an interactive diagnostic toolbar directly on the MultiBuffered sublayer
 * (onDrawMultiBufferedLayer) without touching or causing contention on the hardware front-buffer.
 */

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <functional>
#include "inspectorV2.0/app_brain.hpp"
#include "inspector/modeler_inspector.hpp"
#include "inspector/width_inspector.hpp"

namespace Folio::V2 {

class UiManager {
public:
    static UiManager& Instance() {
        static UiManager s_instance;
        return s_instance;
    }

    void Init() {
        if (m_initialized) return;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding    = 8.0f;
        style.FrameRounding     = 6.0f;
        style.GrabRounding      = 6.0f;
        style.FramePadding      = ImVec2(10.0f, 6.0f);
        style.ItemSpacing       = ImVec2(10.0f, 8.0f);
        style.ScaleAllSizes(1.5f); // High-DPI tablet scaling

#if defined(__ANDROID__)
        ImGui_ImplOpenGL3_Init("#version 300 es");
#else
        ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
        m_initialized = true;
    }

    void Shutdown() {
        if (!m_initialized) return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        m_initialized = false;
    }

    /**
     * @brief Ingests screen pointer for ImGui interaction.
     * @return true if ImGui consumed/captured the touch (e.g. clicked a button).
     */
    bool ProcessTouch(float x, float y, bool isDown, bool isUp) {
        if (!m_initialized) return false;

        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(x, y);
        if (isDown) {
            io.MouseDown[0] = true;
        }
        if (isUp) {
            io.MouseDown[0] = false;
        }

        return io.WantCaptureMouse;
    }

    /**
     * @brief Renders the inspector HUD onto the current OpenGL ES multi-buffered framebuffer.
     */
    void Render(int width, int height) {
        if (!m_initialized || width <= 0 || height <= 0) return;

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DeltaTime = 1.0f / 60.0f;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        // Top-left floating control panel
        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Always);

        if (ImGui::Begin("Folio Inking Lab & Inspector", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            auto& layerMgr = AppBrain::Instance().GetLayerManager();
            auto& canvas = layerMgr.GetCanvasState();
            auto& style = layerMgr.GetActiveStyle();
            auto& brush = layerMgr.GetActiveBrush();
            auto& engine = layerMgr.GetLiveEngine();
            const auto& strokes = layerMgr.GetDocumentStrokes();

            ImGui::Text("FPS: %.1f (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Sub-2ms Zero-Latency Engine");
            ImGui::Separator();

            if (ImGui::Button("Reset View")) {
                canvas.panX = 0.0f;
                canvas.panY = 0.0f;
                canvas.zoom = 1.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Canvas")) {
                layerMgr.ClearDocument();
                if (onClearRequested) {
                    onClearRequested();
                }
            }
            ImGui::Text("Zoom: %.2fx | Pan: (%.1f, %.1f)", canvas.zoom, canvas.panX, canvas.panY);
            ImGui::Text("Strokes on canvas: %zu", strokes.size());
            ImGui::Text("Live Inking Active: %s", layerMgr.IsLiveInkingActive() ? "YES" : "NO");

            if (ImGui::BeginTabBar("InspectorTabs", ImGuiTabBarFlags_None)) {
                // Tab 1: Brush & Playground
                if (ImGui::BeginTabItem("1. Brush & Pen")) {
                    ImGui::SeparatorText("Tool Preset");
                    static int activeBrushSlot = 0;
                    const char* brushPresets[] = { "Pen", "Pencil", "Highlighter" };
                    for (int b = 0; b < 3; ++b) {
                        if (b > 0) ImGui::SameLine();
                        if (ImGui::RadioButton(brushPresets[b], activeBrushSlot == b)) {
                            activeBrushSlot = b;
                            if (b == 0) { // Pen
                                style.baseWidth = 10.0f;
                                style.color = Folio::ColorRgba(0, 150, 255, 255);
                                brush.opacity = 1.0f;
                                brush.unionOverlap = true;
                                style.usePressure = true;
                                style.useVelocity = false;
                            } else if (b == 1) { // Pencil
                                style.baseWidth = 4.0f;
                                style.color = Folio::ColorRgba(180, 180, 190, 255);
                                brush.opacity = 0.65f;
                                brush.unionOverlap = false;
                                style.usePressure = true;
                                style.useVelocity = true;
                            } else if (b == 2) { // Highlighter
                                style.baseWidth = 32.0f;
                                style.color = Folio::ColorRgba(255, 235, 50, 120);
                                brush.opacity = 0.45f;
                                brush.unionOverlap = true;
                                style.usePressure = false;
                                style.useVelocity = false;
                            }
                        }
                    }

                    ImGui::SeparatorText("Geometry & Dynamics");
                    ImGui::SliderFloat("Base Width", &style.baseWidth, 1.0f, 60.0f, "%.1f px");
                    ImGui::Checkbox("Use Pressure", &style.usePressure);
                    ImGui::SameLine();
                    ImGui::Checkbox("Use Velocity Thinning", &style.useVelocity);

                    float col4[4] = {
                        style.color.r / 255.0f, style.color.g / 255.0f,
                        style.color.b / 255.0f, style.color.a / 255.0f
                    };
                    if (ImGui::ColorEdit4("Pigment Color", col4, ImGuiColorEditFlags_NoInputs)) {
                        style.color = Folio::ColorRgba(
                            static_cast<uint8_t>(col4[0] * 255.0f),
                            static_cast<uint8_t>(col4[1] * 255.0f),
                            static_cast<uint8_t>(col4[2] * 255.0f),
                            static_cast<uint8_t>(col4[3] * 255.0f)
                        );
                    }
                    ImGui::SliderFloat("Opacity", &brush.opacity, 0.05f, 1.0f, "%.2f");
                    ImGui::Checkbox("Union Self-Overlap (no double-darkening)", &brush.unionOverlap);

                    ImGui::EndTabItem();
                }

                // Tab 2: Modeler & Prediction (from inspector/modeler_inspector)
                if (ImGui::BeginTabItem("2. Modeler & Physics")) {
                    InkingConfig cfg = engine.GetConfig();
                    if (m_modelerInspector.RenderUI(cfg)) {
                        engine.SetConfig(cfg);
                    }
                    ImGui::EndTabItem();
                }

                // Tab 3: Width Inspector (from inspector/width_inspector)
                if (ImGui::BeginTabItem("3. Width Dynamics")) {
                    InkingConfig cfg = engine.GetConfig();
                    std::span<const StrokePoint> pts = engine.GetLiveModeledPoints();
                    if (pts.empty() && !strokes.empty()) {
                        pts = strokes.back().points;
                    }
                    if (m_widthInspector.RenderUI(cfg, style, pts)) {
                        engine.SetConfig(cfg);
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    std::function<void()> onClearRequested;

private:
    UiManager() = default;
    ~UiManager() { Shutdown(); }

    UiManager(const UiManager&) = delete;
    UiManager& operator=(const UiManager&) = delete;

    Folio::Debug::ModelerInspector m_modelerInspector;
    Folio::Debug::WidthInspector   m_widthInspector;
    bool m_initialized = false;
};

} // namespace Folio::V2
