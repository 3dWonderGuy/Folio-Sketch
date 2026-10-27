#pragma once
/**
 * =========================================================================================
 * @file src/debug/geometry_inspector.hpp
 * @brief Step 4: Geometry Tessellator vs. Outline Polygon, View Modes, and Defect Telemetry
 * =========================================================================================
 */

#include <imgui.h>
#include <span>
#include <cstdint>
#include "folio_sketch/ink_config.hpp"
#include "folio_sketch/ink_stroke_data.hpp"
#include "folio_sketch/ink_mesh.hpp"
#include "folio_sketch/ink_path.hpp"
#include "debug/raw_sensor_layer.hpp"

namespace Folio::Debug {

/**
 * @enum GeometryViewMode
 * @brief Unified View Mode Selector for Step 4.
 */
enum class GeometryViewMode : int {
    DiscsOnly           = 0, ///< Mode 0: Overlapping evaluated circles; zero polygon/mesh logic
    CpuOutlinePolygon   = 1, ///< Mode 1: CPU Outline Polygon (ThorVG / BLPath boundary contour)
    SolidGpuMesh        = 2, ///< Mode 2: Solid GPU Mesh (Standard filled InkMesh)
    WireframeOnly       = 3, ///< Mode 3: Wireframe Only (Triangle edges rendered in neon green)
    SolidPlusWireframe  = 4, ///< Mode 4: Solid + Wireframe Overlay
    MiterNormalNeedles  = 5  ///< Mode 5: Miter & Normal Needles (incoming, outgoing, clamped bisectors)
};

/**
 * @struct MeshDefectStats
 * @brief Real-time telemetry & defect counts computed across InkMesh triangles.
 */
struct MeshDefectStats {
    size_t vertexCount = 0;
    size_t indexCount = 0;
    size_t triangleCount = 0;
    size_t invertedTriangleCount = 0; ///< Triangles with 2D signed area <= 0
    size_t degenerateSliverCount = 0; ///< Triangles with 2D signed area < 1e-4
    double minSignedArea = 0.0;
    double maxSignedArea = 0.0;
};

/**
 * @class GeometryInspector
 * @brief Controls view mode selection, live geometry tessellation sliders,
 *        mesh defect analysis, and 2D overlay rendering for wireframe, outlines, discs, and needles.
 */
class GeometryInspector {
public:
    GeometryInspector();

    /**
     * @brief Evaluates 2D signed area of every triangle in mesh.indices and updates telemetry.
     */
    void AnalyzeMesh(const InkMesh& mesh);

    /**
     * @brief Renders ImGui view mode selector, live geometry sliders, and defect telemetry.
     * @param config Active InkingConfig.
     * @param style Active StrokeStyle (capType, pattern).
     * @return True if any geometry tessellation parameter changed (requires mesh re-tessellation).
     */
    bool RenderUI(InkingConfig& config, StrokeStyle& style);

    /**
     * @brief Renders canvas overlays for the active GeometryViewMode (Discs, CPU Outline, Wireframe, Needles).
     *        Note: Solid GPU Mesh fill in Modes 2 & 4 is rendered by the OpenGL shader pass before ImGui.
     */
    void RenderCanvasOverlays(ImDrawList* drawList,
                              std::span<const StrokePoint> points,
                              const InkPath& outlinePath,
                              const InkMesh& mesh,
                              const CanvasTransform& transform) const;

    [[nodiscard]] GeometryViewMode GetViewMode() const noexcept { return m_viewMode; }
    void SetViewMode(GeometryViewMode mode) noexcept { m_viewMode = mode; }

    [[nodiscard]] bool ShouldRenderSolidGpuMesh() const noexcept {
        return m_viewMode == GeometryViewMode::SolidGpuMesh ||
               m_viewMode == GeometryViewMode::SolidPlusWireframe ||
               (m_viewMode == GeometryViewMode::MiterNormalNeedles && m_showMeshBehindNeedles);
    }

    [[nodiscard]] const MeshDefectStats& GetDefectStats() const noexcept { return m_stats; }

    /**
     * @brief Synchronizes current InkingConfig geometry fields into InkMeshBuilder::SetTessellationParams.
     */
    static void SyncMeshBuilderParams(const InkingConfig& config) noexcept;

private:
    GeometryViewMode m_viewMode = GeometryViewMode::SolidPlusWireframe;
    MeshDefectStats m_stats{};
    bool m_highlightInvertedTriangles = true;
    bool m_showSkirtWireframe = true;
    bool m_showMeshBehindNeedles = true;
};

} // namespace Folio::Debug
