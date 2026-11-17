#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/testbed/testbed_samples.hpp
 * @brief Benchmark & Defect Repro Datasets for Modeler Diagnostics
 * =========================================================================================
 */

namespace Folio::V2 {

class LayerManager;

void InjectTestScribble(LayerManager& layerManager);
void LoadRepro114(LayerManager& layerManager);

} // namespace Folio::V2
