#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/ui/inspector_gui.hpp
 * @brief Dear ImGui HUD, Workspaces, and Diagnostic Controls for Inspector V2.0
 * =========================================================================================
 */

#include <vector>
#include <imgui.h>

namespace Folio::V2 {

class LayerManager;

struct InspectorGui {
    // 1. Modeler live diagnostic toggles
    bool showRawPolyline          = true;
    bool showRawCrosshairs        = true;
    bool showRawPoints            = false;
    bool showModeledCenterline    = true;
    bool showKalmanPrediction     = true;
    bool showRadiusDiscs          = false;
    bool showSolidMeshInModeler   = false;

    // Simulation / Experiment latency injection (affects live inking dispatch across all tabs)
    float simulatedLatencyMs      = 0.0f;

    // 2. Post-processing toggles
    bool showRdpOriginalPoints    = true;
    bool showRdpKeyVertices       = true;

    // 3. Shape recognition overlays
    bool showRecognizedShapeOverlay = true;
    bool showShapeBoundingBox       = true;

    // Workspace & theme selection
    int currentWorkspaceTab       = 0; // 0: Modeler, 1: Width & Dynamics, 2: Post-Processing, 3: Shape Snapping, 4: Canvas & Document, 5: Brush Studio (Cahier)
    int canvasThemePreset         = 0; // 0: Dark Charcoal Grid (#171A1F), 1: AMOLED Black, 2: Blueprint Blue, 3: Light Paper
    bool funBrushMultiStroke      = true; // Allow multi-stroke drawing while in Fun Brushes tab

    std::vector<float> arcLengthPlotSamples;

    void Render(LayerManager& layerManager, int fbWidth, int fbHeight, bool& needsRedraw);
    void RenderTelemetryHud(LayerManager& layerManager, bool drawOpaqueBackdrop = false,
                            int* outX = nullptr, int* outY = nullptr, int* outW = nullptr, int* outH = nullptr);
};

// Internal tab renderers (implemented in inspectorV2.0/ui/tabs/)
void RenderTabModeler(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw);
void RenderTabDynamics(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw);
void RenderTabPostProcessing(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw);
void RenderTabShapeSnapping(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw);
void RenderTabCanvas(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw);
void RenderTabFunBrushes(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw);

// Canvas overlay rendering (implemented in inspectorV2.0/ui/diagnostics_overlay.cpp)
void RenderDiagnosticsOverlay(const InspectorGui& gui, LayerManager& layerManager, int fbWidth, int fbHeight, bool drawBackgroundGrid = true);

} // namespace Folio::V2
