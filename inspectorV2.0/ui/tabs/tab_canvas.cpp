/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_canvas.cpp
 * @brief Workspace 5: Canvas Themes, Navigation & Active Brush Styles
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include <imgui.h>

namespace Folio::V2 {

void RenderTabCanvas(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("5. Canvas & Document")) {
        if (gui.currentWorkspaceTab != 4) {
            gui.currentWorkspaceTab = 4;
            needsRedraw = true;
        }

        auto& canvas = layerManager.GetCanvasState();
        auto& style = layerManager.GetActiveStyle();
        auto& brush = layerManager.GetActiveBrush();
        bool styleDirty = false;
        bool brushDirty = false;

        ImGui::Text("Zoom: %.2fx", canvas.zoom);
        ImGui::Text("Pan: (%.1f, %.1f)", canvas.panX, canvas.panY);
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

        ImGui::Separator();
        ImGui::Text("Committed Strokes: %zu", layerManager.GetDocumentStrokes().size());
        ImGui::Text("Live Inking Active: %s", layerManager.IsLiveInkingActive() ? "YES" : "NO");

        ImGui::SeparatorText("Canvas Background & Theme");
        const char* themePresets[] = {
            "Dark Charcoal Blueprint (#171A1F) [Default]",
            "Deep AMOLED Black (#0B0C0E)",
            "Blueprint Slate Blue (#131B2A)",
            "Classic Light Paper (#F5F5F8)"
        };
        if (ImGui::Combo("Theme Preset", &gui.canvasThemePreset, themePresets, IM_ARRAYSIZE(themePresets))) {
            switch (gui.canvasThemePreset) {
                case 0: canvas.paperColor = Folio::ColorRgba(23, 26, 31, 255); break; // Dark Charcoal
                case 1: canvas.paperColor = Folio::ColorRgba(11, 12, 14, 255); break; // AMOLED Black
                case 2: canvas.paperColor = Folio::ColorRgba(19, 27, 42, 255); break; // Blueprint Blue
                case 3: canvas.paperColor = Folio::ColorRgba(245, 245, 248, 255); break; // Light Paper
            }
            needsRedraw = true;
        }

        float bgCol[3] = {
            static_cast<float>(canvas.paperColor.r) / 255.0f,
            static_cast<float>(canvas.paperColor.g) / 255.0f,
            static_cast<float>(canvas.paperColor.b) / 255.0f
        };
        if (ImGui::ColorEdit3("Custom Canvas Background", bgCol)) {
            canvas.paperColor = Folio::ColorRgba(
                static_cast<uint8_t>(bgCol[0] * 255.0f),
                static_cast<uint8_t>(bgCol[1] * 255.0f),
                static_cast<uint8_t>(bgCol[2] * 255.0f),
                255
            );
            needsRedraw = true;
        }

        ImGui::SeparatorText("Brush Controls");
        styleDirty |= ImGui::SliderFloat("Stroke Width", &style.baseWidth, 1.0f, 50.0f, "%.1f px");

        float col[3] = {
            static_cast<float>(style.color.r) / 255.0f,
            static_cast<float>(style.color.g) / 255.0f,
            static_cast<float>(style.color.b) / 255.0f
        };
        if (ImGui::ColorEdit3("Brush Color", col)) {
            style.color = Folio::ColorRgba(
                static_cast<uint8_t>(col[0] * 255.0f),
                static_cast<uint8_t>(col[1] * 255.0f),
                static_cast<uint8_t>(col[2] * 255.0f),
                255
            );
            styleDirty = true;
        }
        brushDirty |= ImGui::SliderFloat("Brush Opacity", &brush.opacity, 0.05f, 1.0f, "%.2f");
        brushDirty |= ImGui::SliderFloat("Paper Grain Tooth", &brush.grain, 0.0f, 1.0f, "%.2f");

        if (styleDirty || brushDirty) {
            if (!layerManager.GetDocumentStrokes().empty()) {
                auto& strokes = const_cast<std::vector<Folio::V2::DocumentStroke>&>(layerManager.GetDocumentStrokes());
                strokes.back().brush = brush;
                strokes.back().color = Folio::PackPremultipliedGpuColor(style.color);
            }
            needsRedraw = true;
        }

        ImGui::EndTabItem();
    }
}

} // namespace Folio::V2
