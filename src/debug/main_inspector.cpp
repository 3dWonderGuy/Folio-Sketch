/**
 * =========================================================================================
 * @file src/debug/main_inspector.cpp
 * @brief Interactive Multi-Stage Inking Diagnostic Playground & Inspector for Folio-Sketch
 * =========================================================================================
 *
 * Stages:
 *   Step 1: Raw Sensor HUD & Input Capture (src/debug/raw_sensor_layer.hpp/.cpp)
 *   Step 2: Google Stroke Modeler Inspector (src/debug/modeler_inspector.hpp/.cpp)
 *   Step 3: Width, Pressure & Velocity Evaluator (src/debug/width_inspector.hpp/.cpp)
 *   Step 4: Geometry Tessellator vs. Outline Polygon (src/debug/geometry_inspector.hpp/.cpp)
 *   Step 5: Time-Travel Point Scrubber (src/debug/stroke_scrubber.hpp/.cpp)
 *   Step 6: Main Event Loop Integration & OpenGL/GLES3 InkMesh Renderer
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#endif

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "folio_sketch/folio_sketch.hpp"
#include "geometry/ink_stroke_filter.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include "debug/raw_sensor_layer.hpp"
#include "debug/modeler_inspector.hpp"
#include "debug/width_inspector.hpp"
#include "debug/geometry_inspector.hpp"
#include "debug/stroke_scrubber.hpp"
#include "folio_sketch/gpu/gl_capsule_renderer.hpp"

using namespace Folio;
using namespace Folio::Debug;

namespace {

// =============================================================================
// OpenGL / GLES 3.0+ Hardware InkMesh Renderer
// =============================================================================
class GlInkMeshRenderer {
public:
    bool Init() {
#if defined(__ANDROID__)
        const char* glslHeader = "#version 300 es\nprecision highp float;\n";
#else
        const char* glslHeader = "#version 330 core\n";
#endif
        std::string vSrc = std::string(glslHeader) +
            "layout(location = 0) in vec2 aPos;\n"
            "layout(location = 1) in vec2 aEdgeDistRadius;\n"
            "layout(location = 2) in vec4 aColor;\n"
            "uniform vec4 uTransform; // (panX, panY, zoom, unused)\n"
            "uniform vec2 uViewport;  // (width, height)\n"
            "out vec4 vColor;\n"
            "out vec2 vEdgeParams;\n"
            "void main() {\n"
            "    vec2 screenPos = aPos * uTransform.z + uTransform.xy;\n"
            "    vec2 ndc = vec2(\n"
            "        (screenPos.x / uViewport.x) * 2.0 - 1.0,\n"
            "        1.0 - (screenPos.y / uViewport.y) * 2.0\n"
            "    );\n"
            "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
            "    vColor = aColor;\n"
            "    vEdgeParams = vec2(aEdgeDistRadius.x, aEdgeDistRadius.y * uTransform.z);\n"
            "}\n";

        std::string fSrc = std::string(glslHeader) +
            "in vec4 vColor;\n"
            "in vec2 vEdgeParams;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    float edgeDist = abs(vEdgeParams.x);\n"
            "    float radiusPx = max(vEdgeParams.y, 0.5);\n"
            "    float edgeAlpha = 1.0;\n"
            "    if (edgeDist > 1.0) {\n"
            "        float u = clamp((edgeDist - 1.0) * radiusPx / 3.0, 0.0, 1.0);\n"
            "        edgeAlpha = 1.0 - u * u * (3.0 - 2.0 * u);\n"
            "    }\n"
            "    float a = vColor.a * edgeAlpha;\n"
            "    fragColor = vec4(vColor.rgb * a, a);\n"
            "}\n";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vSrc.c_str());
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fSrc.c_str());
        if (!vs || !fs) return false;

        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint linked = 0;
        glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[512];
            glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
            std::cerr << "[GlInkMeshRenderer] Link failed: " << log << std::endl;
            return false;
        }

        m_locTransform = glGetUniformLocation(m_program, "uTransform");
        m_locViewport  = glGetUniformLocation(m_program, "uViewport");

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ibo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(InkVertex),
                              reinterpret_cast<const void*>(offsetof(InkVertex, x)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(InkVertex),
                              reinterpret_cast<const void*>(offsetof(InkVertex, edgeDist)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(InkVertex),
                              reinterpret_cast<const void*>(offsetof(InkVertex, color)));

        glBindVertexArray(0);
        return true;
    }

    void Shutdown() {
        if (m_ibo) { glDeleteBuffers(1, &m_ibo); m_ibo = 0; }
        if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
        if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
        if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    }

    void RenderMesh(const InkMesh& mesh, const CanvasTransform& transform, int vpWidth, int vpHeight) {
        if (!m_program || mesh.empty() || vpWidth <= 0 || vpHeight <= 0) return;

        glUseProgram(m_program);
        glUniform4f(m_locTransform, transform.panX, transform.panY, transform.zoom, 0.0f);
        glUniform2f(m_locViewport, static_cast<float>(vpWidth), static_cast<float>(vpHeight));

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(InkVertex)),
                     mesh.vertices.data(), GL_STREAM_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                     mesh.indices.data(), GL_STREAM_DRAW);

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        // Premultiplied alpha blending
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
    }

private:
    static GLuint CompileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << "[GlInkMeshRenderer] Shader compile error: " << log << std::endl;
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ibo = 0;
    GLint  m_locTransform = -1;
    GLint  m_locViewport  = -1;
};

// Re-runs raw hardware samples through InkEngine to update modeled points when Modeler physics sliders change
void ReplayRawSamplesThroughEngine(InkEngine& engine,
                                   const std::vector<RawInputSample>& rawSamples,
                                   const StrokeStyle& style,
                                   float zoomScale,
                                   FinishedStrokeData& outFinished) {
    if (rawSamples.empty()) return;

    const auto& s0 = rawSamples.front();
    engine.BeginStroke(s0.x, s0.y, s0.pressure, s0.timestampSec, style, zoomScale, s0.tiltRad, 0.0f);
    for (size_t i = 1; i < rawSamples.size(); ++i) {
        const auto& s = rawSamples[i];
        engine.AppendPoint(s.x, s.y, s.pressure, s.timestampSec, s.tiltRad, 0.0f);
    }
    outFinished = engine.FinishStroke();
}

void DrawBackgroundGrid(ImDrawList* drawList, const CanvasTransform& transform, int winW, int winH) {
    if (!drawList) return;
    const float stepPx = std::clamp(50.0f * transform.zoom, 16.0f, 200.0f);
    const ImU32 gridCol = IM_COL32(34, 38, 46, 255);
    const float startX = std::fmod(transform.panX, stepPx);
    const float startY = std::fmod(transform.panY, stepPx);

    for (float x = startX; x < static_cast<float>(winW); x += stepPx) {
        drawList->AddLine(ImVec2(x, 0.0f), ImVec2(x, static_cast<float>(winH)), gridCol, 1.0f);
    }
    for (float y = startY; y < static_cast<float>(winH); y += stepPx) {
        drawList->AddLine(ImVec2(0.0f, y), ImVec2(static_cast<float>(winW), y), gridCol, 1.0f);
    }
}

void DumpStrokeDiagnostics(const char* label,
                           const std::vector<RawInputSample>& rawSamples,
                           const std::vector<StrokePoint>& modeledPts,
                           const InkMesh& mesh,
                           std::ostream& out) {
    out << "=== STROKE DIAGNOSTICS: " << label << " ===\n";
    out << "Raw samples: " << rawSamples.size()
        << " | Modeled pts: " << modeledPts.size()
        << " | Mesh vertices: " << mesh.vertices.size()
        << " | Mesh tris: " << (mesh.indices.size() / 3)
        << " (core: " << (mesh.coreIndexCount / 3) << ")\n";

    auto cleanPts = InkInternal::StrokeFilter::PreFilterPoints(modeledPts, true);
    InkInternal::StrokeFilter::RegularizeRadiusSlope(cleanPts);
    auto segs = InkInternal::ComputeSegments(cleanPts);
    out << "CleanPts: " << cleanPts.size() << " | Segs: " << segs.size() << "\n";

    for (size_t i = 0; i < cleanPts.size(); ++i) {
        double turnDeg = 0.0;
        double dot = 1.0;
        double cross = 0.0;
        if (i > 0 && i + 1 < cleanPts.size() && i < segs.size()) {
            cross = segs[i - 1].ux * segs[i].uy - segs[i - 1].uy * segs[i].ux;
            dot   = segs[i - 1].ux * segs[i].ux + segs[i - 1].uy * segs[i].uy;
            turnDeg = std::atan2(std::abs(cross), dot) * (180.0 / 3.14159265358979323846);
        }
        double segLen = (i < segs.size()) ? segs[i].len : 0.0;
        if (turnDeg >= 5.0 || i < 3 || i + 3 >= cleanPts.size()) {
            out << "  cleanPt[" << i << "] = (" << std::fixed << std::setprecision(3)
                << cleanPts[i].x << ", " << cleanPts[i].y << ") r=" << cleanPts[i].r
                << " segLen=" << segLen << " turnDeg=" << turnDeg
                << (cross < 0.0 ? " [LEFT-IN]" : (cross > 0.0 ? " [RIGHT-IN]" : ""))
                << (dot < -0.866 ? " [HAIRPIN-SPLIT!]" : "")
                << "\n";
        }
    }

    // Print core ribbon triangles around ALL sharp turn vertices (turnDeg >= 20)
    for (size_t k = 1; k + 1 < cleanPts.size() && k < segs.size(); ++k) {
        double cross = segs[k - 1].ux * segs[k].uy - segs[k - 1].uy * segs[k].ux;
        double dot   = segs[k - 1].ux * segs[k].ux + segs[k - 1].uy * segs[k].uy;
        double turnDeg = std::atan2(std::abs(cross), dot) * (180.0 / 3.14159265358979323846);
        if (turnDeg < 20.0) continue;
        // Skip if adjacent to previous printed vertex
        if (k > 1) {
            double pCross = segs[k - 2].ux * segs[k - 1].uy - segs[k - 2].uy * segs[k - 1].ux;
            double pDot   = segs[k - 2].ux * segs[k - 1].ux + segs[k - 2].uy * segs[k - 1].uy;
            if (std::atan2(std::abs(pCross), pDot) * (180.0 / 3.14159265358979323846) >= 20.0) continue;
        }
        double cx = cleanPts[k].x;
        double cy = cleanPts[k].y;
        double searchR = std::max(15.0, cleanPts[k].r * 2.5);
        out << "  [Corner neighborhood around cleanPt[" << k << "] = (" << cx << ", " << cy << ") turnDeg=" << turnDeg << "]:\n";
        for (size_t i = 0; i + 2 < mesh.coreIndexCount; i += 3) {
            uint32_t i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
            const auto& v0 = mesh.vertices[i0];
            const auto& v1 = mesh.vertices[i1];
            const auto& v2 = mesh.vertices[i2];
            if (std::hypot(v0.x - cx, v0.y - cy) <= searchR ||
                std::hypot(v1.x - cx, v1.y - cy) <= searchR ||
                std::hypot(v2.x - cx, v2.y - cy) <= searchR) {
                int centerCount = (v0.edgeDist == 0.0f) + (v1.edgeDist == 0.0f) + (v2.edgeDist == 0.0f);
                if (centerCount >= 2) {
                    out << "    coreTri#" << (i / 3) << " v(" << i0 << "," << i1 << "," << i2 << "): "
                        << "(" << v0.x << "," << v0.y << " ed=" << v0.edgeDist << ") - "
                        << "(" << v1.x << "," << v1.y << " ed=" << v1.edgeDist << ") - "
                        << "(" << v2.x << "," << v2.y << " ed=" << v2.edgeDist << ")\n";
                }
            }
        }
    }

    size_t invCore = 0, invSkirt = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];
        const auto& v0 = mesh.vertices[i0];
        const auto& v1 = mesh.vertices[i1];
        const auto& v2 = mesh.vertices[i2];
        double area = 0.5 * ((static_cast<double>(v1.x) - v0.x) * (static_cast<double>(v2.y) - v0.y) -
                             (static_cast<double>(v1.y) - v0.y) * (static_cast<double>(v2.x) - v0.x));
        if (area <= 0.0) {
            bool isCore = (i < mesh.coreIndexCount);
            if (isCore) ++invCore; else ++invSkirt;
            if (invCore + invSkirt <= 15) {
                out << "  [INVERTED " << (isCore ? "CORE" : "SKIRT") << " TRI #" << (i / 3)
                    << "] area=" << std::scientific << area << std::fixed
                    << " v(" << i0 << "," << i1 << "," << i2 << "): "
                    << "(" << v0.x << "," << v0.y << " ed=" << v0.edgeDist << ") -> "
                    << "(" << v1.x << "," << v1.y << " ed=" << v1.edgeDist << ") -> "
                    << "(" << v2.x << "," << v2.y << " ed=" << v2.edgeDist << ")\n";
            }
        }
    }
    out << "TOTAL INVERTED: " << (invCore + invSkirt)
        << " (core=" << invCore << ", skirt=" << invSkirt << ")\n\n";
}

int RunHeadlessDiagnostics() {
    InkEngine engine;
    GeometryInspector geomInspector; // Also runs SyncMeshBuilderParams(defaultCfg) just like GUI!
    StrokeStyle style;
    style.baseWidth   = 8.0f;
    style.capType     = CapType::Round;
    style.pattern     = StrokePattern::Solid;
    style.color       = ColorRgba(65, 165, 255, 255);
    style.usePressure = true;

    auto runCornerTest = [&](const char* name, double angleDeg, double cornerPauseSteps) {
        std::vector<RawInputSample> samples;
        double t = 0.0;
        double dt = 0.00831; // 120 Hz like user's screenshot
        double x = 100.0, y = 200.0;
        // Horizontal leg going slightly up-right (-8 deg, like screenshot)
        double a1 = -8.0 * 3.14159265358979323846 / 180.0;
        for (int i = 0; i < 15; ++i) {
            double step = (i >= 12) ? 2.5 : 8.0;
            x += step * std::cos(a1);
            y += step * std::sin(a1);
            samples.push_back({ static_cast<float>(x), static_cast<float>(y), 0.65f, 0.0f, t });
            t += dt;
        }
        for (int p = 0; p < cornerPauseSteps; ++p) {
            x += 0.4 * std::cos(a1);
            y += 0.4 * std::sin(a1);
            samples.push_back({ static_cast<float>(x), static_cast<float>(y), 0.65f, 0.0f, t });
            t += dt;
        }
        // Second leg turning down by angleDeg
        double a2 = a1 + angleDeg * 3.14159265358979323846 / 180.0;
        for (int i = 0; i < 15; ++i) {
            double step = (i < 3) ? 2.5 : 8.0;
            x += step * std::cos(a2);
            y += step * std::sin(a2);
            samples.push_back({ static_cast<float>(x), static_cast<float>(y), 0.65f, 0.0f, t });
            t += dt;
        }
        samples.back().pressure = 0.0f;

        FinishedStrokeData finished;
        ReplayRawSamplesThroughEngine(engine, samples, style, 3.7795f, finished);
        DumpStrokeDiagnostics(name, samples, finished.modeledPoints, finished.mesh, std::cout);
    };

    runCornerTest("Corner 90 deg (fast)", 90.0, 0);
    runCornerTest("Corner 90 deg (with 2 slow corner samples)", 90.0, 2);
    runCornerTest("Corner 90 deg (sharp stop-and-turn, 5 slow corner samples)", 90.0, 5);
    runCornerTest("Corner 96 deg (slightly acute L-corner, fast)", 96.0, 0);
    runCornerTest("Corner 96 deg (with 2 slow corner samples)", 96.0, 2);
    runCornerTest("Corner 101 deg (sharp stop-and-turn, 5 slow corner samples)", 101.5, 5);
    runCornerTest("Corner 110 deg (acute corner)", 110.0, 1);
    runCornerTest("Corner 122 deg (sharp stop-and-turn, 5 slow corner samples)", 121.8, 5);
    runCornerTest("Corner 135 deg (sharp V-corner)", 135.0, 1);
    runCornerTest("Corner 165 deg (hairpin reversal)", 165.0, 1);

    // Multi-corner Zig-Zag + S-curve + Loop stress test
    {
        std::vector<RawInputSample> samples;
        double t = 0.0, dt = 0.00831;
        auto addSeg = [&](double x0, double y0, double x1, double y1, int steps) {
            for (int i = 0; i < steps; ++i) {
                double u = static_cast<double>(i) / steps;
                samples.push_back({ static_cast<float>(x0 + (x1 - x0) * u),
                                    static_cast<float>(y0 + (y1 - y0) * u), 0.7f, 0.0f, t });
                t += dt;
            }
        };
        addSeg(100, 100, 220, 100, 15);
        addSeg(220, 100, 120, 180, 15);
        addSeg(120, 180, 240, 220, 15);
        addSeg(240, 220, 240, 110, 15);
        addSeg(240, 110, 340, 110, 15);
        addSeg(340, 110, 340, 260, 15);
        samples.back().pressure = 0.0f;
        FinishedStrokeData finished;
        ReplayRawSamplesThroughEngine(engine, samples, style, 3.7795f, finished);
        DumpStrokeDiagnostics("Multi-corner ZigZag + Box", samples, finished.modeledPoints, finished.mesh, std::cout);
    }

    // Also test repro114
    {
        StrokeScrubber scrubber;
        std::vector<RawInputSample> raw114;
        scrubber.LoadRepro114(style, raw114, nullptr);
        auto slice = scrubber.GetActiveModeledSlice();
        std::vector<StrokePoint> pts(slice.begin(), slice.end());
        DumpStrokeDiagnostics("repro114 (slice 125)", raw114, pts, scrubber.GetScrubbedMesh(), std::cout);
    }

    // Exact 39-point stroke from user's GUI dump: ~180 deg U-turn with soft curve at end
    {
        const double coords[39][2] = {
            {1302.957, 450.944}, {1304.132, 452.301}, {1304.726, 453.921}, {1305.060, 455.489},
            {1305.243, 457.246}, {1305.260, 459.029}, {1305.245, 460.736}, {1305.239, 462.369},
            {1305.239, 464.259}, {1305.273, 466.094}, {1305.353, 467.730}, {1305.485, 469.589},
            {1305.712, 471.532}, {1306.024, 473.544}, {1306.472, 475.826}, {1306.795, 477.417},
            {1307.253, 479.663}, {1307.674, 481.643}, {1308.026, 483.328}, {1308.423, 485.147},
            {1308.863, 486.908}, {1309.295, 488.559}, {1309.775, 490.322}, {1310.266, 492.084},
            {1310.715, 493.703}, {1311.184, 495.278}, {1311.797, 496.823}, {1312.965, 495.659},
            {1313.081, 494.048}, {1313.078, 492.409}, {1313.078, 490.738}, {1312.957, 489.095},
            {1312.962, 487.476}, {1312.962, 485.866}, {1312.962, 484.237}, {1312.962, 482.582},
            {1313.146, 480.915}, {1313.507, 479.347}, {1315.110, 477.273}
        };
        std::vector<RawInputSample> rawDummy;
        std::vector<StrokePoint> pts;
        for (int i = 0; i < 39; ++i) {
            StrokePoint sp{};
            sp.x = static_cast<float>(coords[i][0]);
            sp.y = static_cast<float>(coords[i][1]);
            sp.r = 4.574f;
            pts.push_back(sp);
        }
        InkMesh mesh;
        DumpStrokeDiagnostics("User 180-deg U-turn with soft curve at end", rawDummy, pts, mesh, std::cout);
    }

    // Exact 38-point cursive loop stroke from user's GUI dump (/tmp/folio_stroke_debug.txt)
    {
        const double coords[38][2] = {
            {957.673, 414.305}, {957.981, 415.880}, {959.713, 415.843}, {961.503, 415.100},
            {962.930, 414.364}, {965.030, 413.161}, {966.646, 412.190}, {968.645, 410.928},
            {970.782, 409.488}, {972.929, 407.940}, {975.055, 406.282}, {977.109, 404.523},
            {979.148, 402.610}, {980.943, 400.773}, {982.386, 399.175}, {983.480, 397.832},
            {984.601, 396.254}, {985.455, 394.692}, {986.045, 393.111}, {984.367, 394.224},
            {982.935, 395.704}, {981.737, 397.001}, {980.414, 398.579}, {979.062, 400.394},
            {977.795, 402.294}, {976.759, 404.153}, {975.721, 406.403}, {974.702, 409.114},
            {973.702, 412.292}, {973.158, 414.314}, {972.619, 416.505}, {972.087, 418.813},
            {971.563, 421.188}, {971.059, 423.556}, {970.575, 425.902}, {970.115, 428.214},
            {966.984, 456.713}, {967.034, 458.339}
        };
        std::vector<RawInputSample> rawDummy;
        std::vector<StrokePoint> pts;
        for (int i = 0; i < 38; ++i) {
            StrokePoint sp{};
            sp.x = static_cast<float>(coords[i][0]);
            sp.y = static_cast<float>(coords[i][1]);
            sp.r = 4.574f;
            pts.push_back(sp);
        }
        InkMesh mesh;
        DumpStrokeDiagnostics("User Cursive Loop Stroke", rawDummy, pts, mesh, std::cout);
    }

    // Benchmark 1,000-point CapsuleInstance CPU ingestion (< 0.05 ms target)
    {
        double capsuleMs = GlCapsuleRenderer::BenchmarkCpuIngestionMs(1000, 50);
        std::cout << "=== GPU CAPSULE SDF 1,000-POINT CPU BENCHMARK ===\n"
                  << "1,000-point BuildCapsuleInstances avg CPU time: " << std::fixed << std::setprecision(4)
                  << capsuleMs << " ms (Target < 0.0500 ms: " << (capsuleMs < 0.05 ? "PASS" : "WARN") << ")\n";
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--diagnose") == 0) {
            return RunHeadlessDiagnostics();
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "[folio_ink_inspector] SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

#if defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "Folio-Sketch — Inking Diagnostic Playground & Inspector",
        1600, 960,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << "[folio_ink_inspector] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "[folio_ink_inspector] SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGuiStyle& imStyle = ImGui::GetStyle();
    imStyle.WindowRounding = 7.0f;
    imStyle.FrameRounding  = 4.0f;
    imStyle.GrabRounding   = 4.0f;

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
#if defined(__ANDROID__)
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    GlInkMeshRenderer glMeshRenderer;
    glMeshRenderer.Init();

    GlCapsuleRenderer glCapsuleRenderer;
    glCapsuleRenderer.Init();

    // Core Inking Engine & Diagnostic Inspector Modules
    InkEngine         engine;
    StrokeStyle       strokeStyle;
    strokeStyle.baseWidth   = 8.0f;
    strokeStyle.capType     = CapType::Round;
    strokeStyle.pattern     = StrokePattern::Solid;
    strokeStyle.color       = ColorRgba(65, 165, 255, 255);
    strokeStyle.usePressure = true;
    strokeStyle.useVelocity = false;
    strokeStyle.useTilt     = false;

    RawSensorHUD      rawSensorHud;
    ModelerInspector  modelerInspector;
    WidthInspector    widthInspector;
    GeometryInspector geometryInspector;
    StrokeScrubber    strokeScrubber;
    CanvasTransform   viewport{};

    FinishedStrokeData lastFinishedStroke;
    std::vector<RawInputSample> lastRawSamples;

    // Brush Lab state (pen / pencil / highlighter behaviour testing)
    enum BrushKind { kBrushPen = 0, kBrushPencil = 1, kBrushHighlighter = 2 };
    int          brushKind = kBrushPen;
    CapsuleBrush capsuleBrush{};
    capsuleBrush.unionOverlap = true;

    auto applyBrushPreset = [&](int kind) {
        brushKind = kind;
        switch (kind) {
        case kBrushPencil:
            strokeStyle.baseWidth   = 5.0f;
            strokeStyle.capType     = CapType::Round;
            strokeStyle.pattern     = StrokePattern::TexturedPencil;
            strokeStyle.color       = ColorRgba(55, 55, 62, 255);
            strokeStyle.usePressure = true;
            capsuleBrush = CapsuleBrush{0.85f, 0.65f, 0.6f, true};
            break;
        case kBrushHighlighter:
            strokeStyle.baseWidth   = 26.0f;
            strokeStyle.capType     = CapType::Square;
            strokeStyle.pattern     = StrokePattern::Solid;
            strokeStyle.color       = ColorRgba(255, 235, 59, 255);
            strokeStyle.usePressure = false;
            capsuleBrush = CapsuleBrush{0.35f, 0.0f, 0.6f, true};
            break;
        default:
            strokeStyle.baseWidth   = 8.0f;
            strokeStyle.capType     = CapType::Round;
            strokeStyle.pattern     = StrokePattern::Solid;
            strokeStyle.color       = ColorRgba(65, 165, 255, 255);
            strokeStyle.usePressure = true;
            capsuleBrush = CapsuleBrush{1.0f, 0.0f, 0.6f, true};
            break;
        }
    };

    // Rebuilds the vector outline and GPU capsules for the last finished stroke.
    auto rebuildLastStrokeMesh = [&]() {
        if (lastFinishedStroke.modeledPoints.empty()) return;
        lastFinishedStroke.outlinePath = InkEngine::BuildStrokeOutline(
            lastFinishedStroke.modeledPoints, strokeStyle.capType, strokeStyle.pattern);
        InkEngine::BuildStrokeCapsules(
            lastFinishedStroke.modeledPoints, PackPremultipliedGpuColor(strokeStyle.color),
            lastFinishedStroke.capsules);
    };

    bool isPanning = false;
    bool running = true;
    int  dirtyFrames = 4; // Render initial frames then sleep when idle

    while (running) {
        SDL_Event event;
        bool hadEvent = false;
        if (dirtyFrames > 0) {
            hadEvent = SDL_PollEvent(&event);
        } else {
            hadEvent = SDL_WaitEventTimeout(&event, 50);
        }

        while (hadEvent) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            dirtyFrames = std::max(dirtyFrames, 3);

            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                running = false;
                break;
            }

            // Canvas Panning & Zooming (Right/Middle mouse drag to pan, mouse wheel to zoom)
            if (!io.WantCaptureMouse) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE)) {
                    isPanning = true;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                           (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE)) {
                    isPanning = false;
                } else if (event.type == SDL_EVENT_MOUSE_MOTION && isPanning) {
                    viewport.panX += event.motion.xrel;
                    viewport.panY += event.motion.yrel;
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    float factor = (event.wheel.y > 0.0f) ? 1.15f : (1.0f / 1.15f);
                    float mouseX = io.MousePos.x;
                    float mouseY = io.MousePos.y;
                    ImVec2 beforeWorld = viewport.ScreenToWorld(mouseX, mouseY);
                    viewport.zoom = std::clamp(viewport.zoom * factor, 0.2f, 40.0f);
                    viewport.panX = mouseX - beforeWorld.x * viewport.zoom;
                    viewport.panY = mouseY - beforeWorld.y * viewport.zoom;
                }
            } else {
                isPanning = false;
            }

            // Route stylus and mouse inputs through RawSensorHUD & InkEngine when ImGui does not capture mouse
            if (!io.WantCaptureMouse && !isPanning) {
                RawInputSample sample{};
                int strokeEvt = 0;
                if (rawSensorHud.HandleEvent(event, viewport, sample, strokeEvt)) {
                    if (strokeEvt == 1) {
                        // Pen/Mouse Down -> Start live stroke
                        strokeScrubber.Unfreeze();
                        engine.BeginStroke(sample.x, sample.y, sample.pressure, sample.timestampSec,
                                           strokeStyle, viewport.zoom,
                                           rawSensorHud.GetCurrentTiltXRad(),
                                           rawSensorHud.GetCurrentTiltYRad());
                    } else if (strokeEvt == 2) {
                        // Pen/Mouse Motion while down -> Append point
                        engine.AppendPoint(sample.x, sample.y, sample.pressure, sample.timestampSec,
                                           rawSensorHud.GetCurrentTiltXRad(),
                                           rawSensorHud.GetCurrentTiltYRad());
                    } else if (strokeEvt == 3) {
                        // Pen/Mouse Up -> Finish stroke
                        lastFinishedStroke = engine.FinishStroke();
                        lastRawSamples = rawSensorHud.GetActiveOrLastStroke();
                        if (strokeScrubber.IsAutoFreezeOnLift() && !lastFinishedStroke.modeledPoints.empty()) {
                            strokeScrubber.FreezeStroke(lastRawSamples,
                                                        lastFinishedStroke.modeledPoints,
                                                        strokeStyle,
                                                        viewport.zoom);
                        }
                    }
                }
            }

            hadEvent = SDL_PollEvent(&event);
        }

        if (!running) break;

        // Skip rendering if nothing is dirty and ImGui is not interacting
        if (dirtyFrames <= 0 && !ImGui::IsAnyItemActive() && !engine.IsStrokeActive()) {
            continue;
        }
        if (ImGui::IsAnyItemActive() || engine.IsStrokeActive()) {
            dirtyFrames = 2;
        } else {
            --dirtyFrames;
        }

        // Start ImGui Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int fbWidth = 1600, fbHeight = 960;
        SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
        int winWidth = 1600, winHeight = 960;
        SDL_GetWindowSize(window, &winWidth, &winHeight);

        InkingConfig& config = engine.GetConfig();

        // Determine active stroke data (Live in-flight vs Frozen Scrubber vs Last Finished)
        const InkMesh* activeMesh = nullptr;
        const InkPath* activeOutline = nullptr;
        std::span<const StrokePoint> activeModeledPts{};
        std::span<const StrokePoint> activePredictedPts{};
        InkPath liveOutlineTemp;
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(455.0f, static_cast<float>(winHeight - 24)), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Inking Diagnostic Playground & Inspector")) {
            // Viewport & Color quick bar
            if (ImGui::Button("Reset View (1:1)")) {
                viewport = CanvasTransform{};
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Canvas")) {
                engine.CancelStroke();
                lastFinishedStroke = FinishedStrokeData{};
                lastRawSamples.clear();
                rawSensorHud.ClearSamples();
                strokeScrubber.Unfreeze();
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to Raw-Only")) {
                modelerInspector.ResetOverlays();
                widthInspector.SetShowRadiusDiscs(false);
                geometryInspector.ResetView();
                strokeScrubber.Unfreeze();
                strokeScrubber.SetAutoFreezeOnLift(false);
                rawSensorHud.SetShowCrosshairs(true);
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            ImGui::Text("Zoom: %.2fx", viewport.zoom);

            float col4[4] = {
                strokeStyle.color.r / 255.0f,
                strokeStyle.color.g / 255.0f,
                strokeStyle.color.b / 255.0f,
                strokeStyle.color.a / 255.0f
            };
            if (ImGui::ColorEdit4("Stroke Pigment", col4, ImGuiColorEditFlags_NoInputs)) {
                strokeStyle.color = ColorRgba(
                    static_cast<uint8_t>(col4[0] * 255.0f),
                    static_cast<uint8_t>(col4[1] * 255.0f),
                    static_cast<uint8_t>(col4[2] * 255.0f),
                    static_cast<uint8_t>(col4[3] * 255.0f));
                if (strokeScrubber.IsFrozen()) {
                    strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                }
                dirtyFrames = 2;
            }

            // Quick view bar: brush + render path selection
            ImGui::SeparatorText("Brush & View");
            {
                const char* brushNames[] = { "Pen", "Pencil", "Highlighter" };
                for (int b = 0; b < 3; ++b) {
                    if (b > 0) ImGui::SameLine();
                    if (ImGui::RadioButton(brushNames[b], brushKind == b)) {
                        applyBrushPreset(b);
                        if (strokeScrubber.IsFrozen()) {
                            strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                        } else {
                            rebuildLastStrokeMesh();
                        }
                        dirtyFrames = 2;
                    }
                }
            }
            if (ImGui::Button("GPU Capsule SDF")) {
                geometryInspector.SetEnabled(true);
                geometryInspector.SetViewMode(GeometryViewMode::GpuCapsuleSdf);
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Mesh + Wireframe")) {
                geometryInspector.SetEnabled(true);
                geometryInspector.SetViewMode(GeometryViewMode::SolidPlusWireframe);
                if (strokeScrubber.IsFrozen()) {
                    strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                } else {
                    rebuildLastStrokeMesh();
                }
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Re-Run Modeler")) {
                if (!lastRawSamples.empty()) {
                    ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                    if (strokeScrubber.IsFrozen()) {
                        strokeScrubber.FreezeStroke(lastRawSamples, lastFinishedStroke.modeledPoints, strokeStyle, viewport.zoom);
                    }
                }
                modelerInspector.SetShowModeledCenterline(true);
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Show Widths")) {
                widthInspector.SetShowRadiusDiscs(true);
                dirtyFrames = 2;
            }

            ImGui::Spacing();

            // -----------------------------------------------------------------
            // Tab-Based Navigation: Stages isolated into clean submenu tabs
            // -----------------------------------------------------------------
            if (ImGui::BeginTabBar("InkingPipelineTabBar", ImGuiTabBarFlags_None)) {
                // TAB 0: Brush Lab (pen / pencil / highlighter mechanics)
                if (ImGui::BeginTabItem("Brush Lab")) {
                    bool brushChanged = false;
                    ImGui::TextWrapped(
                        "Pencil: pressure-stable width, paper-tooth grain pinned to canvas space.\n"
                        "Highlighter: constant width, translucent. With union ON, self-overlap never "
                        "darkens; turn it OFF to see the naive per-capsule accumulation artifact.");

                    brushChanged |= ImGui::SliderFloat("Brush Width", &strokeStyle.baseWidth, 1.0f, 60.0f, "%.1f");
                    brushChanged |= ImGui::Checkbox("Use Pressure", &strokeStyle.usePressure);
                    ImGui::SliderFloat("Stroke Opacity", &capsuleBrush.opacity, 0.05f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Grain Strength", &capsuleBrush.grain, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Grain Scale (cells/unit)", &capsuleBrush.grainScale, 0.1f, 3.0f, "%.2f");
                    ImGui::Checkbox("Union Self-Overlap (no double-darkening)", &capsuleBrush.unionOverlap);
                    if (brushKind == kBrushHighlighter) {
                        ImGui::TextDisabled("Capsule SDF draws round ends; the Square nib shows in Mesh modes.");
                    }

                    ImGui::SeparatorText("Self-Overlap Test");
                    if (ImGui::Button("Inject Scribble Stroke")) {
                        std::vector<RawInputSample> scribble;
                        for (int i = 0; i < 260; ++i) {
                            const float t = static_cast<float>(i) * 0.045f;
                            RawInputSample s{};
                            s.x = 520.0f + std::cos(t) * (140.0f + 18.0f * t) * 0.55f + std::sin(t * 2.3f) * 60.0f;
                            s.y = 420.0f + std::sin(t * 1.15f) * 150.0f;
                            s.pressure = 0.35f + 0.5f * (0.5f + 0.5f * std::sin(t * 0.8f));
                            s.tiltRad = 0.0f;
                            s.timestampSec = static_cast<double>(i) * 0.008;
                            scribble.push_back(s);
                        }
                        lastRawSamples = scribble;
                        rawSensorHud.SetStrokeSamples(lastRawSamples);
                        strokeScrubber.Unfreeze();
                        ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                        dirtyFrames = 2;
                    }

                    if (brushChanged) {
                        if (!lastRawSamples.empty() && !engine.IsStrokeActive()) {
                            ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                        }
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // TAB 1: Raw Sensor & Telemetry
                if (ImGui::BeginTabItem("1. Raw Sensor")) {
                    if (rawSensorHud.RenderControls(strokeStyle.baseWidth * 0.5f)) {
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // TAB 2: Modeler & Centerline Physics
                if (ImGui::BeginTabItem("2. Modeler (Physics)")) {
                    if (ImGui::Button("Re-Run Modeler on Captured Samples")) {
                        if (!lastRawSamples.empty()) {
                            ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                            if (strokeScrubber.IsFrozen()) {
                                strokeScrubber.FreezeStroke(lastRawSamples, lastFinishedStroke.modeledPoints, strokeStyle, viewport.zoom);
                            }
                        }
                        modelerInspector.SetShowModeledCenterline(true);
                        dirtyFrames = 2;
                    }

                    if (modelerInspector.RenderUI(config)) {
                        engine.UpdatePhysicsParams();
                        if (!lastRawSamples.empty() && !engine.IsStrokeActive()) {
                            ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                            if (strokeScrubber.IsFrozen()) {
                                strokeScrubber.FreezeStroke(lastRawSamples, lastFinishedStroke.modeledPoints, strokeStyle, viewport.zoom);
                            }
                        }
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // TAB 3: Width & Velocity Evaluation
                if (ImGui::BeginTabItem("3. Width Evaluator")) {
                    std::span<const StrokePoint> ptsForPlot =
                        strokeScrubber.IsFrozen()
                            ? strokeScrubber.GetActiveModeledSlice()
                            : (engine.IsStrokeActive()
                                   ? std::span<const StrokePoint>(engine.GetLiveModeledPoints())
                                   : std::span<const StrokePoint>(lastFinishedStroke.modeledPoints));

                    if (ImGui::Button("Evaluate & Apply Width Dynamics")) {
                        widthInspector.SetShowRadiusDiscs(true);
                        if (!lastRawSamples.empty() && !engine.IsStrokeActive()) {
                            ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                            if (strokeScrubber.IsFrozen()) {
                                strokeScrubber.FreezeStroke(lastRawSamples, lastFinishedStroke.modeledPoints, strokeStyle, viewport.zoom);
                            }
                        } else if (strokeScrubber.IsFrozen()) {
                            WidthInspector::ReevaluateRadii(strokeScrubber.GetMutableFrozenPoints(),
                                                            config, strokeStyle, viewport.zoom);
                            strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                        } else if (!lastFinishedStroke.modeledPoints.empty()) {
                            WidthInspector::ReevaluateRadii(lastFinishedStroke.modeledPoints,
                                                            config, strokeStyle, viewport.zoom);
                            rebuildLastStrokeMesh();
                        }
                        dirtyFrames = 2;
                    }

                    if (widthInspector.RenderUI(config, strokeStyle, ptsForPlot)) {
                        engine.SetConfig(config);
                        if (!lastRawSamples.empty() && !engine.IsStrokeActive()) {
                            ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                            if (strokeScrubber.IsFrozen()) {
                                strokeScrubber.FreezeStroke(lastRawSamples, lastFinishedStroke.modeledPoints, strokeStyle, viewport.zoom);
                            }
                        } else if (strokeScrubber.IsFrozen()) {
                            WidthInspector::ReevaluateRadii(strokeScrubber.GetMutableFrozenPoints(),
                                                            config, strokeStyle, viewport.zoom);
                            strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                        } else if (!lastFinishedStroke.modeledPoints.empty()) {
                            WidthInspector::ReevaluateRadii(lastFinishedStroke.modeledPoints,
                                                            config, strokeStyle, viewport.zoom);
                            rebuildLastStrokeMesh();
                        }
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // TAB 4: Geometry Tessellation & Defect Telemetry
                if (ImGui::BeginTabItem("4. Geometry & Mesh")) {
                    if (geometryInspector.RenderUI(config, strokeStyle)) {
                        if (strokeScrubber.IsFrozen()) {
                            strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                        } else {
                            rebuildLastStrokeMesh();
                        }
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // TAB 5: Time-Travel Point Scrubber & Repro Injector
                if (ImGui::BeginTabItem("5. Scrubber")) {
                    const auto& curModeled = engine.IsStrokeActive()
                                                 ? engine.GetLiveModeledPoints()
                                                 : lastFinishedStroke.modeledPoints;
                    if (strokeScrubber.RenderUI(strokeStyle,
                                                rawSensorHud.GetActiveOrLastStroke(),
                                                curModeled,
                                                lastRawSamples,
                                                viewport,
                                                viewport.zoom)) {
                        rawSensorHud.SetStrokeSamples(lastRawSamples);
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        // Resolve active geometry to display and analyze
        if (engine.IsStrokeActive()) {
            activeModeledPts = engine.GetLiveModeledPoints();
            activePredictedPts = engine.GetPredictedPoints();
            if (geometryInspector.GetViewMode() == GeometryViewMode::CpuOutlinePolygon) {
                std::vector<StrokePoint> tmp(activeModeledPts.begin(), activeModeledPts.end());
                liveOutlineTemp = InkEngine::BuildStrokeOutline(tmp, strokeStyle.capType, strokeStyle.pattern);
                activeOutline = &liveOutlineTemp;
            }
        } else if (strokeScrubber.IsFrozen()) {
            activeMesh = &strokeScrubber.GetScrubbedMesh();
            activeOutline = &strokeScrubber.GetScrubbedOutline();
            activeModeledPts = strokeScrubber.GetActiveModeledSlice();
        } else {
            activeMesh = &lastFinishedStroke.mesh;
            activeOutline = &lastFinishedStroke.outlinePath;
            activeModeledPts = lastFinishedStroke.modeledPoints;
        }

        if (activeMesh) {
            geometryInspector.AnalyzeMesh(*activeMesh);
        }

        // Upload active stroke to GPU Capsule SDF renderer for live telemetry & rendering
        if (!activeModeledPts.empty()) {
            glCapsuleRenderer.UploadStroke(activeModeledPts, PackPremultipliedGpuColor(strokeStyle.color));
            geometryInspector.SetCapsuleTelemetry(
                glCapsuleRenderer.GetInstanceCount(),
                glCapsuleRenderer.GetLastCpuIngestionTimeMs());
            geometryInspector.UpdateShapeRecognition(activeModeledPts, config);
        } else {
            geometryInspector.SetCapsuleTelemetry(0, 0.0);
            geometryInspector.UpdateShapeRecognition({}, config);
        }

        // =====================================================================
        // Canvas Overlays on Background ImDrawList
        // =====================================================================
        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
        DrawBackgroundGrid(bgDrawList, viewport, winWidth, winHeight);

        if (activeMesh) {
            static const InkPath kEmptyPath{};
            geometryInspector.RenderCanvasOverlays(
                bgDrawList,
                activeModeledPts,
                activeOutline ? *activeOutline : kEmptyPath,
                *activeMesh,
                viewport);
        }

        if (geometryInspector.ShouldShowCapsuleBoundingQuads()) {
            glCapsuleRenderer.RenderBoundingQuadsWireframe(bgDrawList, viewport);
        }

        // Width Inspector hollow radius discs overlay (Step 3)
        widthInspector.RenderOverlays(bgDrawList, activeModeledPts, viewport);

        // Modeler Inspector raw dots, yellow centerline, and cyan Kalman trail (Step 2)
        modelerInspector.RenderOverlays(
            bgDrawList,
            rawSensorHud.GetActiveOrLastStroke(),
            activeModeledPts,
            activePredictedPts,
            viewport);

        // Scrubber active cursor vertex highlight (Step 5)
        strokeScrubber.RenderCursorHighlight(bgDrawList, viewport);

        // Raw Sensor HUD & hardware crosshairs (Step 1)
        rawSensorHud.Render(bgDrawList, viewport);

        // =====================================================================
        // OpenGL Render Pass (Background Clear -> Solid GPU InkMesh / Capsule SDF -> ImGui)
        // =====================================================================
        ImGui::Render();
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        if (geometryInspector.ShouldRenderCapsuleSdf() && !activeModeledPts.empty()) {
            glCapsuleRenderer.Render(viewport, winWidth, winHeight, capsuleBrush);
            if (engine.IsStrokeActive() && config.prediction.enable && !activePredictedPts.empty()) {
                glCapsuleRenderer.RenderStroke(activePredictedPts,
                                               PackPremultipliedGpuColor(strokeStyle.color),
                                               viewport, winWidth, winHeight, capsuleBrush);
            }
        } else if (activeMesh && geometryInspector.ShouldRenderSolidGpuMesh()) {
            glMeshRenderer.RenderMesh(*activeMesh, viewport, winWidth, winHeight);
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    glCapsuleRenderer.Shutdown();
    glMeshRenderer.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
