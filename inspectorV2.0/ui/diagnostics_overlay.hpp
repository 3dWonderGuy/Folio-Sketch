#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/ui/diagnostics_overlay.hpp
 * @brief Canvas Background DrawList Diagnostics (Splines, Predictions, Nodes, Shapes)
 * =========================================================================================
 */

namespace Folio::V2 {

class LayerManager;
struct InspectorGui;

void RenderDiagnosticsOverlay(const InspectorGui& gui, LayerManager& layerManager, int fbWidth, int fbHeight);

} // namespace Folio::V2
