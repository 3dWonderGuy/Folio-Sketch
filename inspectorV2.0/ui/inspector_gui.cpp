/**
 * =========================================================================================
 * @file inspectorV2.0/ui/inspector_gui.cpp
 * @brief Main GUI Dispatcher & Telemetry HUD for Inspector V2.0
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include <imgui.h>

namespace Folio::V2 {

void InspectorGui::Render(LayerManager& layerManager, int fbWidth, int fbHeight, bool& needsRedraw) {
    ImGuiIO& io = ImGui::GetIO();

    // Sync testbed mode: Tabs 0, 1, 2, 3 are single-stroke testbeds.
    // Tab 4 (Canvas & Document) is the multi-stroke artwork canvas.
    layerManager.SetTestbedMode(currentWorkspaceTab != 4);

    {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 680.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector V2.0 Controls", nullptr);

        ImGui::Text("FPS: %.1f (%.2f ms/frame)", io.Framerate, (io.Framerate > 0.0f ? (1000.0f / io.Framerate) : 0.0f));
        ImGui::Separator();

        auto& canvas = layerManager.GetCanvasState();

        // Global Quick Actions Toolbar
        if (ImGui::Button("Reset View")) {
            canvas.panX = 0.0f;
            canvas.panY = 0.0f;
            canvas.zoom = 1.0f;
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Canvas (C)")) {
            layerManager.ClearDocument();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-Run Modeler")) {
            layerManager.ReplayRawStroke();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Inject Scribble")) {
            layerManager.InjectTestScribble();
            needsRedraw = true;
        }

        ImGui::Spacing();

        // Quick Inking Color Palette (sets active stroke color for both wet inking and document)
        ImGui::Text("Pen Color:");
        ImGui::SameLine();
        auto& activeStyle = layerManager.GetActiveStyle();
        static const struct { const char* label; ImVec4 col; Folio::ColorRgba rgba; } kPalette[] = {
            { "Blue##pen",   ImVec4(0.0f, 0.45f, 1.0f, 1.0f),  Folio::ColorRgba(0, 115, 255, 255) },
            { "White##pen",  ImVec4(1.0f, 1.0f, 1.0f, 1.0f),  Folio::ColorRgba(255, 255, 255, 255) },
            { "Black##pen",  ImVec4(0.12f, 0.12f, 0.12f, 1.0f),Folio::ColorRgba(25, 25, 25, 255) },
            { "Green##pen",  ImVec4(0.0f, 0.85f, 0.45f, 1.0f), Folio::ColorRgba(0, 220, 110, 255) },
            { "Red##pen",    ImVec4(1.0f, 0.25f, 0.25f, 1.0f), Folio::ColorRgba(255, 65, 65, 255) },
            { "Yellow##pen", ImVec4(1.0f, 0.85f, 0.15f, 1.0f), Folio::ColorRgba(255, 215, 40, 255) },
        };
        for (const auto& item : kPalette) {
            ImGui::PushStyleColor(ImGuiCol_Button, item.col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(item.col.x * 1.15f, item.col.y * 1.15f, item.col.z * 1.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, item.col);
            if (ImGui::Button(item.label, ImVec2(24.0f, 20.0f))) {
                activeStyle.color = item.rgba;
                needsRedraw = true;
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
        }
        ImGui::NewLine();

        if (ImGui::BeginTabBar("InspectorWorkspaces", ImGuiTabBarFlags_None)) {
            RenderTabModeler(*this, layerManager, needsRedraw);
            RenderTabDynamics(*this, layerManager, needsRedraw);
            RenderTabPostProcessing(*this, layerManager, needsRedraw);
            RenderTabShapeSnapping(*this, layerManager, needsRedraw);
            RenderTabCanvas(*this, layerManager, needsRedraw);
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    // Diagnostics overlays (Grid, Centerlines, Predictions, Key Vertices, Recognized Shapes)
    RenderDiagnosticsOverlay(*this, layerManager, fbWidth, fbHeight);

    // Top-Right Live Stylus Telemetry HUD
    {
        const float overlayWidth = 280.0f;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - overlayWidth - 20.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.65f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("Live Stylus Telemetry", nullptr, flags)) {
            const auto& telem = layerManager.GetStylusTelemetry();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "PERFORMANCE");
            ImGui::Text("FPS:           %.1f", io.Framerate);
            ImGui::Text("Frame Latency: %.2f ms", (io.Framerate > 0.0f ? (1000.0f / io.Framerate) : 0.0f));
            if (telem.sampleRateHz > 0.0f) {
                ImGui::Text("Input Latency: %.2f ms", telem.inputLatencyMs);
            } else {
                ImGui::Text("Input Latency: -- ms");
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "STYLUS / INPUT");
            ImGui::Text("Screen Pos:    (%.1f, %.1f)", telem.screenX, telem.screenY);
            ImGui::Text("World Pos:     (%.1f, %.1f)", telem.worldX, telem.worldY);
            ImGui::Text("Pressure:      %.3f", telem.pressure);
            ImGui::ProgressBar(telem.pressure, ImVec2(-1.0f, 12.0f), "");

            const float tiltXDeg = telem.tiltX * 57.2957795f;
            const float tiltYDeg = telem.tiltY * 57.2957795f;
            ImGui::Text("Tilt:          (%.1f°, %.1f°)", tiltXDeg, tiltYDeg);

            ImGui::End();
        }
    }
}

} // namespace Folio::V2
