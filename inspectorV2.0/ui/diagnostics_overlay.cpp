/**
 * =========================================================================================
 * @file inspectorV2.0/ui/diagnostics_overlay.cpp
 * @brief Canvas Background DrawList Diagnostics (Splines, Predictions, Nodes, Shapes)
 * =========================================================================================
 */

#include "inspectorV2.0/ui/diagnostics_overlay.hpp"
#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include "folio_sketch/geometry/shape_recognizer.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Folio::V2 {

void RenderDiagnosticsOverlay(const InspectorGui& gui, LayerManager& layerManager, int fbWidth, int fbHeight, bool drawBackgroundGrid) {
    ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const auto& canvas = layerManager.GetCanvasState();

    float winW = (io.DisplaySize.x > 0.0f) ? io.DisplaySize.x : 1.0f;
    float winH = (io.DisplaySize.y > 0.0f) ? io.DisplaySize.y : 1.0f;
    float scaleX = (static_cast<float>(fbWidth) / winW);
    float scaleY = (static_cast<float>(fbHeight) / winH);

    auto toScreen = [&](float wx, float wy) -> ImVec2 {
        float sx = wx * canvas.zoom + canvas.panX;
        float sy = wy * canvas.zoom + canvas.panY;
        return ImVec2(sx / scaleX, sy / scaleY);
    };

    // 0. Blueprint Background Grid
    if (drawBackgroundGrid) {
        float panScreenX = canvas.panX / scaleX;
        float panScreenY = canvas.panY / scaleY;

        const float stepPx = std::clamp(50.0f * canvas.zoom, 16.0f, 200.0f);
        const ImU32 gridCol = IM_COL32(36, 42, 52, 220);
        float startX = std::fmod(panScreenX, stepPx);
        float startY = std::fmod(panScreenY, stepPx);
        if (startX < 0.0f) startX += stepPx;
        if (startY < 0.0f) startY += stepPx;

        for (float x = startX; x < winW; x += stepPx) {
            bgDrawList->AddLine(ImVec2(x, 0.0f), ImVec2(x, winH), gridCol, 1.0f);
        }
        for (float y = startY; y < winH; y += stepPx) {
            bgDrawList->AddLine(ImVec2(0.0f, y), ImVec2(winW, y), gridCol, 1.0f);
        }
    }

    // Workspace 5 (Canvas & Document) is pure artwork mode: skip all testbed diagnostic overlays
    if (gui.currentWorkspaceTab == 4) {
        return;
    }

    auto& liveEngine = layerManager.GetLiveEngine();
    const auto& livePoints = liveEngine.GetLiveModeledPoints();
    const auto& predPoints = liveEngine.GetPredictedPoints();
    const auto& lastPoints = layerManager.GetLastFinishedPoints();
    const auto& rawSamples = layerManager.GetLastRawSamples();

    const auto& activeCenterline = layerManager.IsLiveInkingActive() ? livePoints : lastPoints;

    // 1-5. Modeler & Centerline Overlays (Active in Workspace 1 & 2)
    if (gui.currentWorkspaceTab == 0 || gui.currentWorkspaceTab == 1) {
        // 1. Raw Input Connected Polyline
        if (gui.showRawPolyline && rawSamples.size() >= 2) {
            const ImU32 lineCol = IM_COL32(140, 145, 155, 190);
            for (size_t i = 1; i < rawSamples.size(); ++i) {
                ImVec2 p0 = toScreen(static_cast<float>(rawSamples[i - 1].x), static_cast<float>(rawSamples[i - 1].y));
                ImVec2 p1 = toScreen(static_cast<float>(rawSamples[i].x), static_cast<float>(rawSamples[i].y));
                bgDrawList->AddLine(p0, p1, lineCol, 1.0f);
            }
        }

        // 2. Raw Hardware Crosshairs (+)
        if (gui.showRawCrosshairs && !rawSamples.empty()) {
            const ImU32 crossCol = IM_COL32(255, 65, 65, 235);
            const float crossHalf = 3.5f;
            for (const auto& s : rawSamples) {
                ImVec2 pt = toScreen(static_cast<float>(s.x), static_cast<float>(s.y));
                bgDrawList->AddLine(ImVec2(pt.x - crossHalf, pt.y), ImVec2(pt.x + crossHalf, pt.y), crossCol, 1.0f);
                bgDrawList->AddLine(ImVec2(pt.x, pt.y - crossHalf), ImVec2(pt.x, pt.y + crossHalf), crossCol, 1.0f);
            }
        }

        // 3. Raw Input Dots (Red Circles)
        if (gui.showRawPoints && !rawSamples.empty()) {
            const ImU32 redDotCol = IM_COL32(255, 55, 55, 245);
            for (const auto& s : rawSamples) {
                ImVec2 pt = toScreen(static_cast<float>(s.x), static_cast<float>(s.y));
                bgDrawList->AddCircleFilled(pt, 2.5f, redDotCol, 12);
            }
        }

        // 4. Yellow Centerline Polyline (Modeled Spline)
        if (gui.showModeledCenterline && activeCenterline.size() >= 2) {
            const ImU32 yellowCol = IM_COL32(255, 235, 40, 240);
            for (size_t i = 1; i < activeCenterline.size(); ++i) {
                ImVec2 p0 = toScreen(static_cast<float>(activeCenterline[i - 1].x), static_cast<float>(activeCenterline[i - 1].y));
                ImVec2 p1 = toScreen(static_cast<float>(activeCenterline[i].x), static_cast<float>(activeCenterline[i].y));
                bgDrawList->AddLine(p0, p1, yellowCol, 2.0f);
            }
        }

        // 5. Cyan Kalman Prediction Trail
        if (gui.showKalmanPrediction && layerManager.IsLiveInkingActive() && predPoints.size() > 1) {
            const ImU32 cyanDotCol = IM_COL32(0, 240, 255, 245);
            const ImU32 cyanLineCol = IM_COL32(0, 220, 245, 180);

            for (size_t i = 1; i < predPoints.size(); ++i) {
                ImVec2 p0 = toScreen(static_cast<float>(predPoints[i - 1].x), static_cast<float>(predPoints[i - 1].y));
                ImVec2 p1 = toScreen(static_cast<float>(predPoints[i].x), static_cast<float>(predPoints[i].y));
                bgDrawList->AddLine(p0, p1, cyanLineCol, 1.5f);
                bgDrawList->AddCircleFilled(p1, 2.5f, cyanDotCol, 12);
            }
        }
    }

    // 6. Hollow Radius Discs (r_i) from Width Inspector (Workspace 2)
    if (gui.currentWorkspaceTab == 1 && gui.showRadiusDiscs && !activeCenterline.empty()) {
        const ImU32 discCol = IM_COL32(255, 165, 60, 175);
        for (const auto& pt : activeCenterline) {
            ImVec2 center = toScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
            float rScreen = std::max(1.0f, pt.r * canvas.zoom);
            bgDrawList->AddCircle(center, rScreen, discCol, 24, 1.0f);
        }
    }

    // 7. RDP Post-Processing Overlays (Workspace 3)
    if (gui.currentWorkspaceTab == 2) {
        if (gui.showRdpOriginalPoints) {
            const auto& rawModeled = layerManager.GetLastRawModeledPoints();
            if (rawModeled.size() >= 2) {
                const ImU32 amberLineCol = IM_COL32(255, 175, 40, 130);
                const ImU32 amberDotCol  = IM_COL32(255, 170, 0, 220);
                for (size_t i = 1; i < rawModeled.size(); ++i) {
                    ImVec2 p0 = toScreen(static_cast<float>(rawModeled[i - 1].x), static_cast<float>(rawModeled[i - 1].y));
                    ImVec2 p1 = toScreen(static_cast<float>(rawModeled[i].x), static_cast<float>(rawModeled[i].y));
                    bgDrawList->AddLine(p0, p1, amberLineCol, 1.0f);
                }
                for (const auto& pt : rawModeled) {
                    ImVec2 p = toScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                    bgDrawList->AddCircleFilled(p, 2.0f, amberDotCol, 8);
                }
            }
        }

        if (gui.showRdpKeyVertices) {
            const auto& keyPts = layerManager.GetLastFinishedPoints();
            if (!keyPts.empty()) {
                const ImU32 greenNodeCol = IM_COL32(50, 255, 80, 245);
                const ImU32 greenHaloCol = IM_COL32(50, 255, 80, 60);
                for (const auto& pt : keyPts) {
                    ImVec2 p = toScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                    bgDrawList->AddCircleFilled(p, 5.5f, greenHaloCol, 16);
                    bgDrawList->AddCircleFilled(p, 2.5f, greenNodeCol, 12);
                    bgDrawList->AddLine(ImVec2(p.x - 4.0f, p.y), ImVec2(p.x + 4.0f, p.y), greenNodeCol, 1.0f);
                    bgDrawList->AddLine(ImVec2(p.x, p.y - 4.0f), ImVec2(p.x, p.y + 4.0f), greenNodeCol, 1.0f);
                }
            }
        }
    }

    // 8. Shape Snapping & Recognition Overlays (Workspace 4)
    if (gui.currentWorkspaceTab == 3 && (gui.showRecognizedShapeOverlay || gui.showShapeBoundingBox) && !activeCenterline.empty()) {
        const auto activeConfig = liveEngine.GetConfig();
        Folio::ShapeRecognitionResult shapeResult = Folio::ShapeRecognizer::ClassifyAndFit(activeCenterline, activeConfig);
        if (shapeResult.type != Folio::RecognizedShapeType::None) {
            if (gui.showRecognizedShapeOverlay && shapeResult.syntheticPoints.size() >= 2) {
                const ImU32 cyanGreenCol = IM_COL32(0, 255, 190, 235);
                const ImU32 cornerCol    = IM_COL32(255, 255, 255, 240);
                const auto& syn = shapeResult.syntheticPoints;

                for (size_t i = 1; i < syn.size(); ++i) {
                    ImVec2 p0 = toScreen(static_cast<float>(syn[i - 1].x), static_cast<float>(syn[i - 1].y));
                    ImVec2 p1 = toScreen(static_cast<float>(syn[i].x), static_cast<float>(syn[i].y));
                    bgDrawList->AddLine(p0, p1, cyanGreenCol, 2.5f);
                }
                if (shapeResult.isClosed && syn.size() > 2) {
                    ImVec2 pEnd   = toScreen(static_cast<float>(syn.back().x), static_cast<float>(syn.back().y));
                    ImVec2 pStart = toScreen(static_cast<float>(syn.front().x), static_cast<float>(syn.front().y));
                    bgDrawList->AddLine(pEnd, pStart, cyanGreenCol, 2.5f);
                }
                for (const auto& pt : syn) {
                    ImVec2 p = toScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                    bgDrawList->AddCircleFilled(p, 3.0f, cornerCol, 8);
                }
            }

            if (gui.showShapeBoundingBox) {
                double minX = activeCenterline[0].x, maxX = activeCenterline[0].x;
                double minY = activeCenterline[0].y, maxY = activeCenterline[0].y;
                for (const auto& pt : activeCenterline) {
                    minX = std::min(minX, pt.x);
                    maxX = std::max(maxX, pt.x);
                    minY = std::min(minY, pt.y);
                    maxY = std::max(maxY, pt.y);
                }
                ImVec2 tl = toScreen(static_cast<float>(minX), static_cast<float>(minY));
                ImVec2 br = toScreen(static_cast<float>(maxX), static_cast<float>(maxY));
                const ImU32 boxCol = IM_COL32(70, 180, 255, 140);
                bgDrawList->AddRect(tl, br, boxCol, 0.0f, 0, 1.0f);

                // Centroid marker
                ImVec2 center = toScreen(static_cast<float>((minX + maxX) * 0.5), static_cast<float>((minY + maxY) * 0.5));
                const ImU32 centerCol = IM_COL32(70, 210, 255, 200);
                bgDrawList->AddLine(ImVec2(center.x - 6.0f, center.y), ImVec2(center.x + 6.0f, center.y), centerCol, 1.5f);
                bgDrawList->AddLine(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x, center.y + 6.0f), centerCol, 1.5f);
            }
        }
    }
}

} // namespace Folio::V2
