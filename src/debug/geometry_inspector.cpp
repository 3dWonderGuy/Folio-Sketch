#include "debug/geometry_inspector.hpp"
#include "geometry/ink_stroke_filter.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio::Debug {

GeometryInspector::GeometryInspector() {
    InkingConfig defaultCfg{};
    SyncMeshBuilderParams(defaultCfg);
}

void GeometryInspector::SyncMeshBuilderParams(const InkingConfig& config) noexcept {
    MeshTessellationParams params{};
    params.cornerAngleThresholdRad = config.cornerAngleThresholdRad;
    params.miterMinClampRatio      = config.miterMinClampRatio;
    params.miterMaxClampRatio      = config.miterMaxClampRatio;
    params.stationaryEnvelopment   = config.stationaryEnvelopment;
    InkMeshBuilder::SetTessellationParams(params);
}

void GeometryInspector::AnalyzeMesh(const InkMesh& mesh) {
    m_stats = MeshDefectStats{};
    m_stats.vertexCount = mesh.vertices.size();
    m_stats.indexCount = mesh.indices.size();
    m_stats.triangleCount = mesh.indices.size() / 3;

    if (mesh.indices.size() < 3 || mesh.vertices.empty()) {
        return;
    }

    bool first = true;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }

        const auto& v0 = mesh.vertices[i0];
        const auto& v1 = mesh.vertices[i1];
        const auto& v2 = mesh.vertices[i2];

        const double dx1 = static_cast<double>(v1.x) - static_cast<double>(v0.x);
        const double dy1 = static_cast<double>(v1.y) - static_cast<double>(v0.y);
        const double dx2 = static_cast<double>(v2.x) - static_cast<double>(v0.x);
        const double dy2 = static_cast<double>(v2.y) - static_cast<double>(v0.y);

        // 2D signed area: 0.5 * ((x1 - x0)*(y2 - y0) - (y1 - y0)*(x2 - x0))
        const double signedArea = 0.5 * (dx1 * dy2 - dy1 * dx2);

        if (first) {
            m_stats.minSignedArea = signedArea;
            m_stats.maxSignedArea = signedArea;
            first = false;
        } else {
            m_stats.minSignedArea = std::min(m_stats.minSignedArea, signedArea);
            m_stats.maxSignedArea = std::max(m_stats.maxSignedArea, signedArea);
        }

        if (signedArea <= 0.0) {
            m_stats.invertedTriangleCount++;
        }
        if (signedArea < 1e-4) {
            m_stats.degenerateSliverCount++;
        }
    }
}

bool GeometryInspector::RenderUI(InkingConfig& config, StrokeStyle& style) {
    bool geomDirty = false;

    ImGui::SeparatorText("Unified View Mode Selector");
    int modeInt = static_cast<int>(m_viewMode);
    const char* modeItems[] = {
        "Mode 0: Discs Only (Evaluated Circles)",
        "Mode 1: CPU Outline Polygon (ThorVG / BLPath)",
        "Mode 2: Solid GPU Mesh (Filled InkMesh)",
        "Mode 3: Wireframe Only (Neon Green Edges)",
        "Mode 4: Solid + Wireframe Overlay",
        "Mode 5: Miter & Normal Needles"
    };
    if (ImGui::Combo("View Mode", &modeInt, modeItems, IM_ARRAYSIZE(modeItems))) {
        m_viewMode = static_cast<GeometryViewMode>(modeInt);
    }

    if (m_viewMode == GeometryViewMode::WireframeOnly || m_viewMode == GeometryViewMode::SolidPlusWireframe) {
        ImGui::Checkbox("Include Exterior AA Skirt in Wireframe", &m_showSkirtWireframe);
        ImGui::SameLine();
        ImGui::Checkbox("Highlight Inverted Tris", &m_highlightInvertedTriangles);
    } else if (m_viewMode == GeometryViewMode::MiterNormalNeedles) {
        ImGui::Checkbox("Render Solid GPU Mesh Behind Needles", &m_showMeshBehindNeedles);
    }

    ImGui::SeparatorText("Nib Cap & Pattern");
    int capInt = static_cast<int>(style.capType);
    const char* capNames[] = { "Round", "Chisel (Calligraphy)", "Square (Highlighter)" };
    if (ImGui::Combo("Cap Type", &capInt, capNames, IM_ARRAYSIZE(capNames))) {
        style.capType = static_cast<CapType>(capInt);
        geomDirty = true;
    }

    int patInt = static_cast<int>(style.pattern);
    const char* patNames[] = { "Solid", "Dashed", "Dotted", "DashDot", "TexturedPencil" };
    if (ImGui::Combo("Stroke Pattern", &patInt, patNames, IM_ARRAYSIZE(patNames))) {
        style.pattern = static_cast<StrokePattern>(patInt);
        geomDirty = true;
    }

    ImGui::SeparatorText("Live Geometry Sliders");
    constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
    constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;

    float cornerDeg = std::clamp(config.cornerAngleThresholdRad * kRadToDeg, 10.0f, 120.0f);
    if (ImGui::SliderFloat("Corner Angle Threshold", &cornerDeg, 10.0f, 120.0f, "%.1f deg")) {
        config.cornerAngleThresholdRad = cornerDeg * kDegToRad;
        SyncMeshBuilderParams(config);
        geomDirty = true;
    }

    if (ImGui::SliderFloat("Miter Min Clamp Ratio", &config.miterMinClampRatio, 0.0f, 1.0f, "%.2fx")) {
        SyncMeshBuilderParams(config);
        geomDirty = true;
    }
    if (ImGui::SliderFloat("Miter Max Clamp Ratio", &config.miterMaxClampRatio, 1.0f, 2.5f, "%.2fx")) {
        SyncMeshBuilderParams(config);
        geomDirty = true;
    }
    if (ImGui::Checkbox("Stationary Envelopment (ds <= |dr|)", &config.stationaryEnvelopment)) {
        SyncMeshBuilderParams(config);
        geomDirty = true;
    }

    ImGui::SeparatorText("Real-Time Telemetry & Defect Detection");
    ImGui::Text("Total Vertices: %zu  |  Total Indices: %zu  |  Triangles: %zu",
                m_stats.vertexCount, m_stats.indexCount, m_stats.triangleCount);

    if (m_stats.invertedTriangleCount > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.22f, 0.22f, 1.0f));
        ImGui::Text("INVERTED TRIANGLES (Signed Area <= 0): %zu  [DEFECT DETECTED!]",
                    m_stats.invertedTriangleCount);
        ImGui::PopStyleColor();
    } else {
        ImGui::TextColored(ImVec4(0.30f, 0.95f, 0.45f, 1.0f),
                           "Inverted Triangles (Signed Area <= 0): 0 (OK)");
    }

    if (m_stats.degenerateSliverCount > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                           "Degenerate Slivers (Signed Area < 1e-4): %zu",
                           m_stats.degenerateSliverCount);
    } else {
        ImGui::Text("Degenerate Slivers (Signed Area < 1e-4): 0");
    }

    ImGui::Text("Signed Area Range: [%.5f, %.3f]", m_stats.minSignedArea, m_stats.maxSignedArea);

    return geomDirty;
}

void GeometryInspector::RenderCanvasOverlays(ImDrawList* drawList,
                                             std::span<const StrokePoint> points,
                                             const InkPath& outlinePath,
                                             const InkMesh& mesh,
                                             const CanvasTransform& transform) const {
    if (!drawList) return;

    // -------------------------------------------------------------------------
    // Mode 0: Discs Only (Overlapping evaluated circles; zero polygon/mesh logic)
    // -------------------------------------------------------------------------
    if (m_viewMode == GeometryViewMode::DiscsOnly) {
        const ImU32 fillCol = IM_COL32(65, 145, 255, 95);
        const ImU32 rimCol  = IM_COL32(95, 180, 255, 220);
        for (const auto& pt : points) {
            ImVec2 center = transform.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
            float rPx = std::max(1.0f, pt.r * transform.zoom);
            drawList->AddCircleFilled(center, rPx, fillCol, 28);
            drawList->AddCircle(center, rPx, rimCol, 28, 1.2f);
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Mode 1: CPU Outline Polygon (ThorVG / BLPath boundary contour)
    // -------------------------------------------------------------------------
    if (m_viewMode == GeometryViewMode::CpuOutlinePolygon) {
        const auto& verbs = outlinePath.verbs();
        const auto& pts   = outlinePath.points();
        if (verbs.empty() || pts.empty()) return;

        const ImU32 contourCol = IM_COL32(255, 130, 40, 245);
        const ImU32 vertexCol  = IM_COL32(255, 220, 90, 225);

        size_t ptIdx = 0;
        ImVec2 subStart(0.0f, 0.0f);
        ImVec2 curPos(0.0f, 0.0f);
        bool hasCur = false;

        for (PathVerb verb : verbs) {
            switch (verb) {
                case PathVerb::MoveTo:
                    if (ptIdx < pts.size()) {
                        curPos = transform.WorldToScreen(pts[ptIdx].x, pts[ptIdx].y);
                        subStart = curPos;
                        hasCur = true;
                        drawList->AddCircleFilled(curPos, 2.0f, vertexCol, 8);
                        ptIdx++;
                    }
                    break;
                case PathVerb::LineTo:
                    if (ptIdx < pts.size()) {
                        ImVec2 nextPos = transform.WorldToScreen(pts[ptIdx].x, pts[ptIdx].y);
                        if (hasCur) {
                            drawList->AddLine(curPos, nextPos, contourCol, 1.8f);
                        }
                        drawList->AddCircleFilled(nextPos, 1.8f, vertexCol, 8);
                        curPos = nextPos;
                        hasCur = true;
                        ptIdx++;
                    }
                    break;
                case PathVerb::CubicTo:
                    if (ptIdx + 2 < pts.size()) {
                        ImVec2 c1 = transform.WorldToScreen(pts[ptIdx].x, pts[ptIdx].y);
                        ImVec2 c2 = transform.WorldToScreen(pts[ptIdx + 1].x, pts[ptIdx + 1].y);
                        ImVec2 p2 = transform.WorldToScreen(pts[ptIdx + 2].x, pts[ptIdx + 2].y);
                        if (hasCur) {
                            drawList->AddBezierCubic(curPos, c1, c2, p2, contourCol, 1.8f);
                        }
                        curPos = p2;
                        hasCur = true;
                        ptIdx += 3;
                    }
                    break;
                case PathVerb::Close:
                    if (hasCur) {
                        drawList->AddLine(curPos, subStart, contourCol, 1.8f);
                    }
                    break;
            }
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Mode 3 & Mode 4: Wireframe Triangle Edges (Neon Green)
    // -------------------------------------------------------------------------
    if (m_viewMode == GeometryViewMode::WireframeOnly ||
        m_viewMode == GeometryViewMode::SolidPlusWireframe) {
        const size_t maxIdx = m_showSkirtWireframe
                                  ? mesh.indices.size()
                                  : std::min<size_t>(mesh.coreIndexCount, mesh.indices.size());
        const ImU32 coreWireCol  = IM_COL32(57, 255, 20, 225);  // Neon green for core triangles
        const ImU32 skirtWireCol = IM_COL32(30, 175, 95, 130);  // Dimmer green for AA skirt triangles
        const ImU32 invFillCol   = IM_COL32(255, 30, 30, 140);
        const ImU32 invEdgeCol   = IM_COL32(255, 50, 50, 255);

        for (size_t i = 0; i + 2 < maxIdx; i += 3) {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                continue;
            }

            const auto& v0 = mesh.vertices[i0];
            const auto& v1 = mesh.vertices[i1];
            const auto& v2 = mesh.vertices[i2];

            double signedArea = 0.5 * ((static_cast<double>(v1.x) - v0.x) * (static_cast<double>(v2.y) - v0.y) -
                                       (static_cast<double>(v1.y) - v0.y) * (static_cast<double>(v2.x) - v0.x));

            ImVec2 p0 = transform.WorldToScreen(v0.x, v0.y);
            ImVec2 p1 = transform.WorldToScreen(v1.x, v1.y);
            ImVec2 p2 = transform.WorldToScreen(v2.x, v2.y);

            if (m_highlightInvertedTriangles && signedArea <= 0.0) {
                drawList->AddTriangleFilled(p0, p1, p2, invFillCol);
                drawList->AddTriangle(p0, p1, p2, invEdgeCol, 2.0f);
            } else {
                ImU32 col = (i < mesh.coreIndexCount) ? coreWireCol : skirtWireCol;
                drawList->AddTriangle(p0, p1, p2, col, 1.0f);
            }
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Mode 5: Miter & Normal Needles (Incoming normals, outgoing normals, clamped bisectors)
    // -------------------------------------------------------------------------
    if (m_viewMode == GeometryViewMode::MiterNormalNeedles) {
        if (points.size() < 2) return;

        const auto& tessParams = InkMeshBuilder::GetTessellationParams();
        std::vector<StrokePoint> cleanPts = InkInternal::StrokeFilter::PreFilterPoints(
            points, tessParams.stationaryEnvelopment);
        InkInternal::StrokeFilter::RegularizeRadiusSlope(cleanPts);
        if (cleanPts.size() < 2) return;

        auto segs = InkInternal::ComputeSegments(cleanPts);
        if (segs.empty()) return;

        const ImU32 inNormCol   = IM_COL32(255, 110, 60, 220);  // Incoming normal (Orange-Red)
        const ImU32 outNormCol  = IM_COL32(60, 195, 255, 220);  // Outgoing normal (Cyan-Blue)
        const ImU32 bisectorCol = IM_COL32(255, 60, 235, 255);  // Clamped bisector (Magenta)
        const ImU32 spineCol    = IM_COL32(200, 200, 210, 150);

        for (size_t i = 1; i < cleanPts.size(); ++i) {
            ImVec2 p0 = transform.WorldToScreen(static_cast<float>(cleanPts[i - 1].x),
                                                static_cast<float>(cleanPts[i - 1].y));
            ImVec2 p1 = transform.WorldToScreen(static_cast<float>(cleanPts[i].x),
                                                static_cast<float>(cleanPts[i].y));
            drawList->AddLine(p0, p1, spineCol, 1.0f);
        }

        const double minClamp = std::clamp(static_cast<double>(tessParams.miterMinClampRatio), 0.0, 1.0);
        const double maxClamp = std::clamp(static_cast<double>(tessParams.miterMaxClampRatio), 1.0, 2.5);

        // Start vertex P_0 outgoing normal needle
        {
            const auto& p0 = cleanPts.front();
            const auto& s0 = segs.front();
            ImVec2 c = transform.WorldToScreen(static_cast<float>(p0.x), static_cast<float>(p0.y));
            ImVec2 l = transform.WorldToScreen(static_cast<float>(p0.x + s0.nx * p0.r),
                                               static_cast<float>(p0.y + s0.ny * p0.r));
            ImVec2 r = transform.WorldToScreen(static_cast<float>(p0.x - s0.nx * p0.r),
                                               static_cast<float>(p0.y - s0.ny * p0.r));
            drawList->AddLine(l, r, outNormCol, 1.0f);
        }

        // Interior vertices P_i: incoming normal, outgoing normal, and clamped bisector needles
        for (size_t i = 1; i + 1 < cleanPts.size(); ++i) {
            const auto& pt   = cleanPts[i];
            const auto& sIn  = segs[i - 1];
            const auto& sOut = segs[i];
            const double r   = std::max(0.01f, pt.r);

            ImVec2 c = transform.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));

            // 1. Incoming normal needle (1px)
            ImVec2 inL = transform.WorldToScreen(static_cast<float>(pt.x + sIn.nx * r),
                                                 static_cast<float>(pt.y + sIn.ny * r));
            ImVec2 inR = transform.WorldToScreen(static_cast<float>(pt.x - sIn.nx * r),
                                                 static_cast<float>(pt.y - sIn.ny * r));
            drawList->AddLine(inL, inR, inNormCol, 1.0f);

            // 2. Outgoing normal needle (1px)
            ImVec2 outL = transform.WorldToScreen(static_cast<float>(pt.x + sOut.nx * r),
                                                  static_cast<float>(pt.y + sOut.ny * r));
            ImVec2 outR = transform.WorldToScreen(static_cast<float>(pt.x - sOut.nx * r),
                                                  static_cast<float>(pt.y - sOut.ny * r));
            drawList->AddLine(outL, outR, outNormCol, 1.0f);

            // 3. Clamped Bisector needle (1px)
            double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
            double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
            double turnAngle = std::atan2(std::abs(cross), dot);

            double avgNx = sIn.nx + sOut.nx;
            double avgNy = sIn.ny + sOut.ny;
            double avgLen = std::hypot(avgNx, avgNy);
            if (avgLen > 1e-6) { avgNx /= avgLen; avgNy /= avgLen; }
            else { avgNx = sIn.nx; avgNy = sIn.ny; }

            double miterScale = 1.0 / std::max(0.25, std::cos(turnAngle * 0.5));
            miterScale = std::clamp(miterScale, minClamp, maxClamp);

            double sinHalf = std::sqrt(std::max(0.0, (1.0 - dot) * 0.5));
            double safeLimit = (std::min(sIn.len, sOut.len) * 0.5) / std::max(1e-4, sinHalf);
            bool leftIsInner = (cross < 0.0) || (cross == 0.0 && dot < 0.0);

            double outerDist = r * miterScale;
            double innerDist = std::min(outerDist, std::max(r * minClamp, safeLimit * 0.85));

            double dLeft  = leftIsInner ? innerDist : outerDist;
            double dRight = leftIsInner ? outerDist : innerDist;

            ImVec2 bL = transform.WorldToScreen(static_cast<float>(pt.x + avgNx * dLeft),
                                                static_cast<float>(pt.y + avgNy * dLeft));
            ImVec2 bR = transform.WorldToScreen(static_cast<float>(pt.x - avgNx * dRight),
                                                static_cast<float>(pt.y - avgNy * dRight));
            drawList->AddLine(c, bL, bisectorCol, 1.0f);
            drawList->AddLine(c, bR, bisectorCol, 1.0f);
            drawList->AddCircleFilled(bL, 2.2f, bisectorCol, 8);
            drawList->AddCircleFilled(bR, 2.2f, bisectorCol, 8);
        }

        // End vertex P_{N-1} incoming normal needle
        {
            const auto& pEnd = cleanPts.back();
            const auto& sEnd = segs.back();
            ImVec2 l = transform.WorldToScreen(static_cast<float>(pEnd.x + sEnd.nx * pEnd.r),
                                               static_cast<float>(pEnd.y + sEnd.ny * pEnd.r));
            ImVec2 r = transform.WorldToScreen(static_cast<float>(pEnd.x - sEnd.nx * pEnd.r),
                                               static_cast<float>(pEnd.y - sEnd.ny * pEnd.r));
            drawList->AddLine(l, r, inNormCol, 1.0f);
        }
    }
}

} // namespace Folio::Debug
