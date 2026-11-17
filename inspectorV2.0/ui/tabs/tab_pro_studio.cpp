/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_pro_studio.cpp
 * @brief Workspace 7: Pro Studio (Raster Dab Brushes, Bristles, Paint Depletion & Canvas Materials)
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include "core/canvas_material.hpp"
#include "pipelines/raster/ink_raster.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdint>

namespace Folio::V2 {

void RenderTabProStudio(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("7. Pro Studio (Materials)")) {
        if (gui.currentWorkspaceTab != 6) {
            gui.currentWorkspaceTab = 6;
            needsRedraw = true;
        }

        auto& material = layerManager.GetCanvasMaterial();
        auto& brush = layerManager.GetActiveRasterBrush();
        auto& style = layerManager.GetActiveStyle();

        bool materialModified = false;
        bool brushModified = false;

        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "PRO DRAWING STUDIO (SHAPE x GRAIN x DEPLETION)");
        ImGui::TextDisabled("Procreate-style raster dab simulation with bristle anisotropy & physical substrate.");

        if (ImGui::Button("Inject Scribble")) {
            layerManager.InjectTestScribble();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Canvas (C)")) {
            layerManager.ClearDocument();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Multi-Stroke Mode", &gui.funBrushMultiStroke)) {
            layerManager.SetTestbedMode(!gui.funBrushMultiStroke);
            needsRedraw = true;
        }

        ImGui::SeparatorText("Canvas Surface Material");

        static const struct {
            const char* label;
            Folio::CanvasMaterial (*factory)();
        } kMaterialPresets[] = {
            { "Smooth Paper",         Folio::CanvasMaterial::SmoothPaper },
            { "Fine Sketchbook",      Folio::CanvasMaterial::SketchbookFine },
            { "Cold-Press Watercolor",Folio::CanvasMaterial::ColdPressWatercolor },
            { "Rough Pastel",         Folio::CanvasMaterial::RoughPastel },
            { "Oil Canvas Weave",     Folio::CanvasMaterial::OilCanvasWeave },
            { "Concrete Wall",        Folio::CanvasMaterial::ConcreteWall }
        };

        int activeMatIdx = layerManager.GetActiveMaterialIndex();
        ImGui::Columns(3, "MatPresetGrid", false);
        for (int i = 0; i < 6; ++i) {
            const bool selected = (activeMatIdx == i);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.32f, 1.0f));
            }
            if (ImGui::Button(kMaterialPresets[i].label, ImVec2(-1.0f, 26.0f))) {
                layerManager.SetActiveMaterialIndex(i);
                material = kMaterialPresets[i].factory();
                materialModified = true;
            }
            if (selected) {
                ImGui::PopStyleColor();
            }
            ImGui::NextColumn();
        }
        ImGui::Columns(1);

        const char* patternNames[] = {
            "Smooth Paper", "Cold-Press Paper", "Rough Pastel Tooth", "Woven Canvas", "Concrete Wall"
        };
        int patIdx = static_cast<int>(material.pattern);
        if (ImGui::Combo("Surface Pattern", &patIdx, patternNames, IM_ARRAYSIZE(patternNames))) {
            material.pattern = static_cast<Folio::SurfaceTexturePattern>(patIdx);
            materialModified = true;
        }
        materialModified |= ImGui::SliderFloat("Tooth Depth", &material.toothDepth, 0.0f, 1.0f, "%.2f");
        materialModified |= ImGui::SliderFloat("Tooth Scale (mm)", &material.toothScaleMm, 0.1f, 4.0f, "%.2f mm");
        materialModified |= ImGui::SliderFloat("Absorbency", &material.absorbency, 0.0f, 1.0f, "%.2f");
        materialModified |= ImGui::SliderFloat("Surface Drag (Friction)", &material.surfaceDrag, 0.0f, 1.0f, "%.2f");

        ImGui::SeparatorText("Pro Brush Presets");

        static constexpr Folio::ProBrushPreset kPresets[] = {
            Folio::ProBrushPreset::HB_Pencil,
            Folio::ProBrushPreset::VineCharcoal,
            Folio::ProBrushPreset::SoftPastel,
            Folio::ProBrushPreset::OilFlatBristle,
            Folio::ProBrushPreset::AcrylicImpasto,
            Folio::ProBrushPreset::WatercolorWash,
            Folio::ProBrushPreset::DryBrush,
            Folio::ProBrushPreset::AerosolSprayCan,
            Folio::ProBrushPreset::PaintSplatter,
            Folio::ProBrushPreset::WetGouacheMixer,
            Folio::ProBrushPreset::PaletteKnife
        };

        int activeBrushIdx = layerManager.GetActiveProPresetIndex();
        ImGui::Columns(3, "ProBrushGrid", false);
        for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kPresets)); ++i) {
            const bool selected = (activeBrushIdx == i);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.85f, 1.0f));
            }
            if (ImGui::Button(Folio::RasterDabBuilder::GetProBrushPresetName(kPresets[i]), ImVec2(-1.0f, 28.0f))) {
                layerManager.SetActiveProPresetIndex(i);
                brush = Folio::RasterDabBuilder::GetProBrushPreset(kPresets[i]);
                brushModified = true;
            }
            if (selected) {
                ImGui::PopStyleColor();
            }
            ImGui::NextColumn();
        }
        ImGui::Columns(1);

        ImGui::SeparatorText("Size, Pigment & Tip Silhouette");
        brushModified |= ImGui::SliderFloat("Brush Size (mm)", &style.baseWidth, 1.0f, 60.0f, "%.1f mm");

        float col[3] = {
            static_cast<float>(style.color.r) / 255.0f,
            static_cast<float>(style.color.g) / 255.0f,
            static_cast<float>(style.color.b) / 255.0f
        };
        if (ImGui::ColorEdit3("Pigment Color", col)) {
            style.color = Folio::ColorRgba(
                static_cast<uint8_t>(col[0] * 255.0f),
                static_cast<uint8_t>(col[1] * 255.0f),
                static_cast<uint8_t>(col[2] * 255.0f),
                255
            );
            brushModified = true;
        }

        const char* tipNames[] = {
            "Round Soft", "Round Hard", "Flat Bristle", "Chalk Speckle", "Charcoal Grain"
        };
        int tipIdx = std::clamp(static_cast<int>(brush.tipShape), 0, 4);
        if (ImGui::Combo("Tip Shape", &tipIdx, tipNames, IM_ARRAYSIZE(tipNames))) {
            brush.tipShape = static_cast<Folio::RasterTipShape>(tipIdx);
            brushModified = true;
        }

        brushModified |= ImGui::SliderFloat("Dab Spacing Ratio", &brush.spacingRatio, 0.03f, 0.60f, "%.2f");
        brushModified |= ImGui::SliderFloat("Edge Hardness", &brush.hardness, 0.0f, 1.0f, "%.2f");
        brushModified |= ImGui::SliderFloat("Aspect Ratio (Bristle Flatness)", &brush.aspectRatio, 0.15f, 1.0f, "%.2f");
        brushModified |= ImGui::SliderFloat("Scatter Jitter", &brush.scatterRatio, 0.0f, 1.5f, "%.2f");
        brushModified |= ImGui::SliderFloat("Angle Jitter (rad)", &brush.angleJitterRad, 0.0f, 3.14159f, "%.2f rad");
        brushModified |= ImGui::SliderFloat("Size Jitter", &brush.sizeJitter, 0.0f, 0.9f, "%.2f");
        brushModified |= ImGui::SliderFloat("Opacity Jitter", &brush.opacityJitter, 0.0f, 0.9f, "%.2f");
        brushModified |= ImGui::SliderFloat("Hue Jitter (deg)", &brush.hueJitterDeg, 0.0f, 45.0f, "%.1f°");
        brushModified |= ImGui::SliderFloat("Lightness Jitter", &brush.lightnessJitter, 0.0f, 0.35f, "%.2f");

        ImGui::SeparatorText("Tilt, Paint Depletion & Wet Mix");
        brushModified |= ImGui::Checkbox("Align to Tangent", &brush.alignToTangent);
        ImGui::SameLine();
        brushModified |= ImGui::Checkbox("Align to Tilt Azimuth", &brush.alignToTilt);
        brushModified |= ImGui::SliderFloat("Tilt Spread Multiplier", &brush.tiltSpreadMultiplier, 1.0f, 3.5f, "%.2fx");
        brushModified |= ImGui::SliderFloat("Pressure -> Opacity", &brush.opacityPressure, 0.0f, 1.0f, "%.2f");
        brushModified |= ImGui::SliderFloat("Brush Grain Depth", &brush.grainDepth, 0.0f, 1.0f, "%.2f");
        brushModified |= ImGui::SliderFloat("Watercolor Wet Edge", &brush.wetEdge, 0.0f, 1.0f, "%.2f");
        brushModified |= ImGui::SliderFloat("Paint Charge Distance (0=Inf)", &brush.chargeDistanceMm, 0.0f, 800.0f, "%.0f mm");
        if (brush.chargeDistanceMm > 0.0f) {
            brushModified |= ImGui::SliderFloat("Min Depleted Opacity", &brush.minDepletedOpacity, 0.0f, 0.8f, "%.2f");
        }
        brushModified |= ImGui::SliderFloat("Velocity Starvation", &brush.velocityThinning, 0.0f, 1.0f, "%.2f");
        brushModified |= ImGui::SliderFloat("Wet Smudge Pull", &brush.smudgePull, 0.0f, 1.0f, "%.2f");

        ImGui::SeparatorText("Raster Telemetry");
        ImGui::Text("Last Stroke Dabs: %zu", layerManager.GetLastRasterDabCount());

        if (materialModified || brushModified) {
            layerManager.RebuildDocumentCustomInstances();
            needsRedraw = true;
        }

        ImGui::EndTabItem();
    }
}

} // namespace Folio::V2
