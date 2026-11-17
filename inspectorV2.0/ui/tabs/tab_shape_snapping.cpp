/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_shape_snapping.cpp
 * @brief Workspace 4: Shape Recognition, Classification & Snap-to-Fit Controls
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include "engine/post_processing/shape_recognizer.hpp"
#include <imgui.h>

namespace Folio::V2 {

void RenderTabShapeSnapping(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("4. Shape Snapping")) {
        if (gui.currentWorkspaceTab != 3) {
            gui.currentWorkspaceTab = 3;
            needsRedraw = true;
        }

        auto& liveEngine = layerManager.GetLiveEngine();
        auto config = liveEngine.GetConfig();
        bool configDirty = false;

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Geometric Shape Recognition & Snap-to-Fit Engine");

        // 1. Live Classification Card
        ImGui::SeparatorText("Live Geometric Classification");
        const auto& testPts = layerManager.IsLiveInkingActive() ? liveEngine.GetLiveModeledPoints() : layerManager.GetLastFinishedPoints();
        Folio::ShapeRecognitionResult shapeResult = Folio::ShapeRecognizer::ClassifyAndFit(testPts, config);

        const char* shapeName = "None / Freehand";
        ImVec4 badgeColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

        switch (shapeResult.type) {
            case Folio::RecognizedShapeType::Line:
                shapeName = "Straight Line";
                badgeColor = ImVec4(0.2f, 0.9f, 0.4f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Circle:
                shapeName = "Perfect Circle";
                badgeColor = ImVec4(0.2f, 0.85f, 1.0f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Ellipse:
                shapeName = "Ellipse";
                badgeColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Triangle:
                shapeName = "Triangle";
                badgeColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Rectangle:
                shapeName = "Rectangle";
                badgeColor = ImVec4(1.0f, 0.5f, 0.2f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Square:
                shapeName = "Square";
                badgeColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Diamond:
                shapeName = "Diamond";
                badgeColor = ImVec4(0.8f, 0.4f, 1.0f, 1.0f);
                break;
            case Folio::RecognizedShapeType::Quadrilateral:
                shapeName = "Quadrilateral (Trap / Para)";
                badgeColor = ImVec4(0.9f, 0.7f, 0.3f, 1.0f);
                break;
            default:
                shapeName = "Freehand / Unclassified";
                badgeColor = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
                break;
        }

        ImGui::Text("Detected Primitive: ");
        ImGui::SameLine();
        ImGui::TextColored(badgeColor, "%s", shapeName);

        ImGui::Text("Confidence Score:   %.1f%%", shapeResult.confidence * 100.0f);
        ImGui::ProgressBar(shapeResult.confidence, ImVec2(-1.0f, 16.0f));

        ImGui::Text("Loop Topology:      %s", shapeResult.isClosed ? "CLOSED LOOP" : "OPEN PATH");
        ImGui::Text("Synthetic Vertices: %zu", shapeResult.syntheticPoints.size());

        // One-Click Snap Action
        ImGui::Spacing();
        bool canSnap = (shapeResult.type != Folio::RecognizedShapeType::None && !shapeResult.syntheticPoints.empty());
        if (!canSnap) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Snap Current Stroke to Recognized Shape", ImVec2(-1.0f, 32.0f))) {
            layerManager.SetLastFinishedPoints(shapeResult.syntheticPoints);
            needsRedraw = true;
        }
        if (!canSnap) {
            ImGui::EndDisabled();
        }

        // 2. Gesture & Snapping Thresholds
        ImGui::SeparatorText("Snapping & Hold Gesture Tuning");
        configDirty |= ImGui::Checkbox("Enable Hold-to-Snap", &config.shapeSnapping.enableHoldToSnap);
        if (config.shapeSnapping.enableHoldToSnap) {
            configDirty |= ImGui::SliderFloat("Hold Dwell Duration (s)",
                                              &config.shapeSnapping.holdDwellDurationSec,
                                              0.1f, 2.0f, "%.2f s");
            configDirty |= ImGui::SliderFloat("Movement Threshold (mm)",
                                              &config.shapeSnapping.movementThresholdMm,
                                              0.2f, 5.0f, "%.2f mm");
        }

        ImGui::SeparatorText("Classification Geometry Thresholds");
        configDirty |= ImGui::SliderFloat("Loop Closure Gap (mm)",
                                          &config.shapeSnapping.loopClosureGapMm,
                                          1.0f, 40.0f, "%.1f mm");
        configDirty |= ImGui::SliderFloat("Line Straightness Ratio",
                                          &config.shapeSnapping.lineStraightnessRatio,
                                          0.005f, 0.15f, "%.3f");
        configDirty |= ImGui::SliderFloat("Circle Fit Ratio",
                                          &config.shapeSnapping.circleFitToleranceRatio,
                                          0.01f, 0.35f, "%.3f");
        configDirty |= ImGui::SliderFloat("Circle/Ellipse Snap Threshold",
                                          &config.shapeSnapping.circleAspectTolerance,
                                          0.05f, 0.50f, "%.2f");
        configDirty |= ImGui::SliderFloat("Ellipse Fit Ratio",
                                          &config.shapeSnapping.ellipseFitToleranceRatio,
                                          0.01f, 0.35f, "%.3f");
        configDirty |= ImGui::SliderFloat("Triangle Fit Ratio",
                                          &config.shapeSnapping.triangleFitToleranceRatio,
                                          0.01f, 0.25f, "%.3f");
        configDirty |= ImGui::SliderFloat("Square Fit Ratio",
                                          &config.shapeSnapping.squareFitToleranceRatio,
                                          0.01f, 0.25f, "%.3f");
        configDirty |= ImGui::SliderFloat("Rectangle Fit Ratio",
                                          &config.shapeSnapping.rectangleFitToleranceRatio,
                                          0.01f, 0.25f, "%.3f");
        configDirty |= ImGui::SliderFloat("Diamond Fit Ratio",
                                          &config.shapeSnapping.diamondFitToleranceRatio,
                                          0.01f, 0.25f, "%.3f");
        configDirty |= ImGui::SliderFloat("Quad (Trap/Para) Fit Ratio",
                                          &config.shapeSnapping.quadFitToleranceRatio,
                                          0.01f, 0.25f, "%.3f");
        configDirty |= ImGui::SliderFloat("Rect 90° Angle Tolerance",
                                          &config.shapeSnapping.rectAngleToleranceDeg,
                                          5.0f, 35.0f, "%.1f°");
        configDirty |= ImGui::SliderFloat("Axis Snap Angle (0=off)",
                                          &config.shapeSnapping.axisSnapAngleDeg,
                                          0.0f, 15.0f, "%.1f°");
        configDirty |= ImGui::SliderFloat("Winner Margin (Min Lead)",
                                          &config.shapeSnapping.winnerMarginRatio,
                                          0.0f, 0.30f, "%.2f");
        configDirty |= ImGui::SliderFloat("Polygon Corner Threshold (deg)",
                                          &config.shapeSnapping.polygonCornerAngleThresholdDeg,
                                          15.0f, 90.0f, "%.1f°");

        // 3. Visual Overlays
        ImGui::SeparatorText("Visual Diagnostic Overlays");
        ImGui::Checkbox("Show Fitted Synthetic Shape Primitive (Cyan-Green)", &gui.showRecognizedShapeOverlay);
        ImGui::Checkbox("Show Bounding Box & Centroid Marker", &gui.showShapeBoundingBox);

        // 4. Test Actions
        ImGui::SeparatorText("Actions & Tests");
        if (ImGui::Button("Re-Run Recognition")) {
            layerManager.ReplayRawStroke();
            needsRedraw = true;
        }

        if (configDirty) {
            liveEngine.SetConfig(config);
            if (!layerManager.IsLiveInkingActive()) {
                layerManager.ReplayRawStroke();
            }
            needsRedraw = true;
        }

        ImGui::EndTabItem();
    }
}

} // namespace Folio::V2
