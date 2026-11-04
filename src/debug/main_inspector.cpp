/**
 * =========================================================================================
 * @file src/debug/main_inspector.cpp
 * @brief Interactive Multi-Stage Inking Diagnostic Playground & Inspector for Folio-Sketch
 * =========================================================================================
 *
 * Inking Lab modes (tabs):
 *   1. Centerline      - Raw Sensor HUD, Modeler (physics / wobble / Kalman prediction), Width dynamics,
 *                        Time-Travel Scrubber. Everything upstream of the vector outline.
 *   2. Pen & Width     - Nib cap, pattern, width + outline post-processing on the vector outline.
 *   3. Post-Processing - Curve simplification (RDP) & Gaussian radius smoothing with Before vs After visual overlays.
 *   4. Shape Snapping  - Shape recognition tuning and hold-to-snap testing.
 *   5. Playground      - Pen / pencil / highlighter finished-product bench with multi-stroke canvas.
 *
 * Supporting modules:
 *   src/debug/raw_sensor_layer.hpp/.cpp, modeler_inspector, width_inspector, geometry_inspector,
 *   stroke_scrubber, and the OpenGL/GLES3 renderers in this file.
 */

#include "debug/gl_loader.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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
#include "debug/gl_capsule_renderer.hpp"
#include "debug/gl_shape_renderer.hpp"

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

    /**
     * @brief Fills arbitrary closed contours (including self-overlapping ones) using a
     *        nonzero-winding stencil-then-cover pass. Used to display vector outlines with real nib caps.
     * @param colorStraightRgba Straight (non-premultiplied) packed RGBA8 color (see PackGpuColor).
     */
    void FillContours(const std::vector<std::vector<PathPoint>>& contours,
                      uint32_t colorStraightRgba,
                      const CanvasTransform& transform, int vpWidth, int vpHeight) {
        if (!m_program || contours.empty() || vpWidth <= 0 || vpHeight <= 0) return;

        m_fillVerts.clear();
        for (const auto& contour : contours) {
            if (contour.size() < 3) continue;
            const PathPoint& a = contour[0];
            for (size_t i = 1; i + 1 < contour.size(); ++i) {
                const PathPoint* tri[3] = { &a, &contour[i], &contour[i + 1] };
                for (const PathPoint* p : tri) {
                    InkVertex v;
                    v.x = p->x;
                    v.y = p->y;
                    v.edgeDist = 0.0f;
                    v.radius = 0.0f;
                    v.color = colorStraightRgba;
                    m_fillVerts.push_back(v);
                }
            }
        }
        if (m_fillVerts.empty()) return;

        glUseProgram(m_program);
        glUniform4f(m_locTransform, transform.panX, transform.panY, transform.zoom, 0.0f);
        glUniform2f(m_locViewport, static_cast<float>(vpWidth), static_cast<float>(vpHeight));

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_fillVerts.size() * sizeof(InkVertex)),
                     m_fillVerts.data(), GL_STREAM_DRAW);

        const GLsizei vertexCount = static_cast<GLsizei>(m_fillVerts.size());

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        // Pass 1: accumulate winding numbers in the stencil buffer (no color writes)
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glClear(GL_STENCIL_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDisable(GL_BLEND);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);

        // Pass 2: cover - paint only where the winding number is nonzero
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);

        glDisable(GL_STENCIL_TEST);
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
    std::vector<InkVertex> m_fillVerts;
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

// =============================================================================
// Inking Lab: modes, brush slots and committed stroke model
// =============================================================================
enum class LabMode : int {
    Centerline     = 0,  ///< Raw sensor -> modeler -> width dynamics (everything before the vector outline)
    PenShape       = 1,  ///< Nib cap, pattern, width settings on the generated vector outline
    PostProcessing = 2,  ///< Post-processing tuning: Before vs After simplification & Gaussian smoothing
    Snapping       = 3,  ///< Shape recognition / hold-to-snap tuning
    Playground     = 4   ///< Finished-product test bench: pen, pencil, highlighter on paper
};

enum class RenderPath : int {
    CapsuleSdf    = 0,  ///< GPU capsule SDF (round caps, supports grain + opacity brush)
    FilledOutline = 1,  ///< Vector outline filled via stencil (shows real nib caps / patterns)
    OutlineContour = 2  ///< Vector outline contour lines only
};

enum class PaperStyle : int { Blank = 0, Grid = 1, Ruled = 2 };

enum BrushKind { kBrushPen = 0, kBrushPencil = 1, kBrushHighlighter = 2 };

struct BrushSlot {
    StrokeStyle  style;
    CapsuleBrush brush;
};

/// A finished stroke that stays on the canvas. Raw samples are kept so every setting change can
/// re-run the whole pipeline (modeler -> width -> snapping -> outline) live.
struct CommittedStroke {
    std::vector<RawInputSample> raw;
    std::vector<StrokePoint>    modeled;   ///< Modeler output prior to any shape snapping
    std::vector<StrokePoint>    points;    ///< Displayed centerline (modeled, or snapped synthetic shape)
    InkPath                     outline;
    StrokeStyle                 style;
    CapsuleBrush                brush;
    int   brushKind = kBrushPen;
    float zoom      = 1.0f;
    bool  wantSnap  = false;  ///< Hold-to-snap was triggered (or snap-on-lift is active)
    bool  snapped   = false;  ///< Recognition succeeded and the centerline was replaced
};

void InitBrushSlot(BrushSlot& slot, int kind) {
    slot = BrushSlot{};
    switch (kind) {
    case kBrushPencil:
        slot.style.baseWidth   = 5.0f;
        slot.style.capType     = CapType::Round;
        slot.style.pattern     = StrokePattern::TexturedPencil;
        slot.style.color       = ColorRgba(55, 55, 62, 255);
        slot.style.usePressure = true;
        slot.style.useTilt     = true;
        slot.brush = CapsuleBrush{0.85f, 0.65f, 0.6f, true};
        break;
    case kBrushHighlighter:
        slot.style.baseWidth   = 26.0f;
        slot.style.capType     = CapType::Square;
        slot.style.pattern     = StrokePattern::Solid;
        slot.style.color       = ColorRgba(255, 235, 59, 255);
        slot.style.usePressure = false;
        slot.brush = CapsuleBrush{0.35f, 0.0f, 0.6f, true};
        break;
    default:
        slot.style.baseWidth   = 8.0f;
        slot.style.capType     = CapType::Round;
        slot.style.pattern     = StrokePattern::Solid;
        slot.style.color       = ColorRgba(65, 165, 255, 255);
        slot.style.usePressure = true;
        slot.brush = CapsuleBrush{1.0f, 0.0f, 0.6f, true};
        break;
    }
}

/// Flattens an InkPath (lines + cubics) into closed polygon contours in world space.
void FlattenOutline(const InkPath& path, std::vector<std::vector<PathPoint>>& contours) {
    contours.clear();
    const auto& verbs = path.verbs();
    const auto& pts   = path.points();
    size_t pi = 0;
    std::vector<PathPoint> cur;
    auto flush = [&]() {
        if (cur.size() >= 3) contours.push_back(cur);
        cur.clear();
    };

    for (PathVerb verb : verbs) {
        switch (verb) {
        case PathVerb::MoveTo:
            flush();
            if (pi < pts.size()) cur.push_back(pts[pi++]);
            break;
        case PathVerb::LineTo:
            if (pi < pts.size()) cur.push_back(pts[pi++]);
            break;
        case PathVerb::CubicTo:
            if (pi + 2 < pts.size() && !cur.empty()) {
                const PathPoint p0 = cur.back();
                const PathPoint c1 = pts[pi];
                const PathPoint c2 = pts[pi + 1];
                const PathPoint p3 = pts[pi + 2];
                constexpr int kSteps = 10;
                for (int s = 1; s <= kSteps; ++s) {
                    const float t = static_cast<float>(s) / static_cast<float>(kSteps);
                    const float u = 1.0f - t;
                    cur.push_back(PathPoint{
                        u * u * u * p0.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * p3.x,
                        u * u * u * p0.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * p3.y });
                }
                pi += 3;
            } else {
                pi = std::min(pi + 3, pts.size());
            }
            break;
        case PathVerb::Close:
            flush();
            break;
        }
    }
    flush();
}

/// Builds thin world-space rectangles (1 screen pixel thick) for the grid / ruled paper background.
void BuildPaperContours(PaperStyle style, const CanvasTransform& t, int winW, int winH,
                        std::vector<std::vector<PathPoint>>& out) {
    out.clear();
    if (style == PaperStyle::Blank) return;

    const float zoom  = std::max(t.zoom, 1e-3f);
    const float step  = (style == PaperStyle::Ruled) ? 32.0f : 50.0f;
    const float thick = 1.0f / zoom;
    const ImVec2 w0 = t.ScreenToWorld(0.0f, 0.0f);
    const ImVec2 w1 = t.ScreenToWorld(static_cast<float>(winW), static_cast<float>(winH));

    auto addRect = [&](float x0, float y0, float x1, float y1) {
        out.push_back(std::vector<PathPoint>{
            PathPoint{x0, y0}, PathPoint{x1, y0}, PathPoint{x1, y1}, PathPoint{x0, y1} });
    };

    const int maxLines = 600;
    int count = 0;
    for (float y = std::floor(w0.y / step) * step; y <= w1.y && count < maxLines; y += step, ++count) {
        addRect(w0.x, y, w1.x, y + thick);
    }
    if (style == PaperStyle::Grid) {
        count = 0;
        for (float x = std::floor(w0.x / step) * step; x <= w1.x && count < maxLines; x += step, ++count) {
            addRect(x, w0.y, x + thick, w1.y);
        }
    }
}

/// Runs shape recognition on a stroke's modeled centerline and, on success, swaps in the fitted shape.
bool ApplySnapToStroke(CommittedStroke& s, const InkingConfig& cfg) {
    if (s.modeled.size() < 2) return false;
    ShapeRecognitionResult r = ShapeRecognizer::ClassifyAndFit(s.modeled, cfg);
    if (r.type == RecognizedShapeType::None || r.syntheticPoints.size() < 2) return false;
    s.points  = std::move(r.syntheticPoints);
    s.outline = InkEngine::BuildStrokeOutline(s.points, s.style.capType, s.style.pattern, true);
    return true;
}

/// Generates a hand-drawn-looking test shape (deterministic jitter) for shape snapping tuning.
/// kind: 0 circle, 1 rectangle, 2 triangle, 3 straight line.
std::vector<RawInputSample> MakeTestShapeSamples(int kind, float cx, float cy) {
    std::vector<RawInputSample> out;
    double t = 0.0;
    const double dt = 1.0 / 120.0;

    auto jitter = [](int i, float amp) {
        return amp * (0.5f * std::sin(static_cast<float>(i) * 12.9898f) +
                      0.5f * std::sin(static_cast<float>(i) * 78.233f + 1.7f));
    };
    auto push = [&](float x, float y) {
        const int i = static_cast<int>(out.size());
        out.push_back({ x + jitter(i, 1.4f), y + jitter(i + 977, 1.4f), 0.7f, 0.0f, t });
        t += dt;
    };
    auto pushPolygon = [&](const std::vector<ImVec2>& verts, int samplesPerEdge) {
        const size_t n = verts.size();
        for (size_t e = 0; e < n; ++e) {
            const ImVec2& a = verts[e];
            const ImVec2& b = verts[(e + 1) % n];
            for (int s = 0; s < samplesPerEdge; ++s) {
                const float u = static_cast<float>(s) / static_cast<float>(samplesPerEdge);
                push(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
            }
        }
        push(verts[0].x + 1.0f, verts[0].y + 1.0f);
    };

    switch (kind) {
    case 0: {
        constexpr int   kN = 96;
        constexpr float kRadius = 85.0f;
        constexpr float kSweep = 0.996f;
        for (int i = 0; i <= kN; ++i) {
            const float a = 6.28318530718f * kSweep * static_cast<float>(i) / static_cast<float>(kN);
            push(cx + std::cos(a) * kRadius, cy + std::sin(a) * kRadius);
        }
        break;
    }
    case 1:
        pushPolygon({ ImVec2(cx - 110.0f, cy - 70.0f), ImVec2(cx + 110.0f, cy - 70.0f),
                      ImVec2(cx + 110.0f, cy + 70.0f), ImVec2(cx - 110.0f, cy + 70.0f) }, 24);
        break;
    case 2:
        pushPolygon({ ImVec2(cx, cy - 90.0f), ImVec2(cx + 100.0f, cy + 70.0f),
                      ImVec2(cx - 100.0f, cy + 70.0f) }, 30);
        break;
    default: {
        constexpr int kN = 60;
        for (int i = 0; i <= kN; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kN);
            push(cx - 130.0f + 260.0f * u, cy + 20.0f - 40.0f * u);
        }
        break;
    }
    }
    return out;
}

void DrawWorldPolyline(ImDrawList* dl, std::span<const StrokePoint> pts, const CanvasTransform& t,
                       ImU32 col, float thickness) {
    if (!dl || pts.size() < 2) return;
    for (size_t i = 1; i < pts.size(); ++i) {
        dl->AddLine(t.WorldToScreen(static_cast<float>(pts[i - 1].x), static_cast<float>(pts[i - 1].y)),
                    t.WorldToScreen(static_cast<float>(pts[i].x), static_cast<float>(pts[i].y)),
                    col, thickness);
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

    if (!InitGlLoader()) {
        std::cerr << "[folio_ink_inspector] Failed to initialize OpenGL functions via SDL_GL_GetProcAddress!" << std::endl;
    }

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

    GlShapeRenderer glShapeRenderer;
    glShapeRenderer.Init();
    std::vector<GpuShapeInstance> liveGpuShapes;

    // Core Inking Engine & Lab Modules
    InkEngine         engine;
    RawSensorHUD      rawSensorHud;
    ModelerInspector  modelerInspector;
    WidthInspector    widthInspector;
    GeometryInspector geometryInspector;
    StrokeScrubber    strokeScrubber;
    CanvasTransform   viewport{};

    // Mode 1 shows the modeled centerline + Kalman spine by default
    modelerInspector.SetShowModeledCenterline(true);
    modelerInspector.SetShowKalmanSpine(true);

    // Brush slots (Pen / Pencil / Highlighter). Every committed stroke stays linked to its slot so
    // tuning a brush live updates all strokes drawn with it.
    BrushSlot brushSlots[3];
    for (int k = 0; k < 3; ++k) InitBrushSlot(brushSlots[k], k);
    int activeBrush = kBrushPen;
    auto curStyle = [&]() -> StrokeStyle& { return brushSlots[activeBrush].style; };
    auto curBrush = [&]() -> CapsuleBrush& { return brushSlots[activeBrush].brush; };

    // Lab state
    LabMode    labMode = LabMode::Centerline;
    bool       modeVisited[5] = { true, false, false, false, false };
    RenderPath renderPath = RenderPath::CapsuleSdf;
    PaperStyle paperStyle = PaperStyle::Grid;
    bool whiteBackground   = false;
    bool multiStroke       = false;  // false: a new stroke replaces the previous one
    bool ghostFill         = false;  // Mode 1: translucent ink fill beneath the centerline
    bool showCenterline    = false;  // Mode 2 / 4 / 5: optional centerline overlay
    bool snapOnLift        = false;  // Mode 4: snap on pen-up even without a hold
    bool showSnapCandidate = true;   // Mode 4: draw what the recognizer would snap to

    // Post-Processing Lab comparison toggles
    bool showBeforePoints       = true;  // Red polyline + dots of raw modeled centerline before post-processing
    bool showAfterPoints        = true;  // Green/Cyan polyline + dots of simplified/smoothed centerline
    bool showBeforeDiscs        = false; // Radii discs before smoothing
    bool showAfterDiscs         = false; // Radii discs after smoothing
    bool isolateComparisonView  = true;  // In PostProcessing mode, render before/after geometry side-by-side or overlaid cleanly

    std::vector<CommittedStroke> strokes;
    std::vector<RawInputSample>  scrubRaw;
    static const std::vector<StrokePoint> kNoPoints;

    // Hold-to-snap dwell tracking (Mode 3 / 4)
    float  holdAnchorX  = 0.0f;
    float  holdAnchorY  = 0.0f;
    double holdStartSec = 0.0;
    bool   snapLatched  = false;
    ShapeRecognitionResult snapPreview;

    auto snapModeActive = [&]() {
        return labMode == LabMode::Snapping || labMode == LabMode::Playground;
    };

    // Re-runs the full pipeline (modeler -> width -> snapping -> outline) for one stroke.
    auto rebuildStroke = [&](CommittedStroke& s) {
        if (s.raw.empty()) return;
        s.style = brushSlots[s.brushKind].style;
        s.brush = brushSlots[s.brushKind].brush;
        FinishedStrokeData fin;
        ReplayRawSamplesThroughEngine(engine, s.raw, s.style, s.zoom, fin);
        s.modeled = !fin.rawModeledPoints.empty() ? std::move(fin.rawModeledPoints) : fin.modeledPoints;
        s.points  = std::move(fin.modeledPoints);
        s.outline = std::move(fin.outlinePath);
        s.snapped = s.wantSnap && ApplySnapToStroke(s, engine.GetConfig());
    };

    // Pushes engine config edits (physics + width evaluator) after a UI change.
    auto applyConfigChange = [&]() { engine.SetConfig(engine.GetConfig()); };

    // Heavy path: re-model every stroke (physics / width / snapping thresholds changed).
    auto replayStrokes = [&]() {
        if (engine.IsStrokeActive()) return;
        for (auto& s : strokes) rebuildStroke(s);
        if (strokeScrubber.IsFrozen() && !strokes.empty()) {
            const auto& l = strokes.back();
            strokeScrubber.FreezeStroke(l.raw, l.modeled, l.style, viewport.zoom);
        }
    };

    // Light path: only brush look changed (color / opacity / grain / cap / pattern).
    auto restyleStrokes = [&](bool rebuildOutlines) {
        for (auto& s : strokes) {
            s.style = brushSlots[s.brushKind].style;
            s.brush = brushSlots[s.brushKind].brush;
            if (rebuildOutlines) {
                s.outline = InkEngine::BuildStrokeOutline(s.points, s.style.capType, s.style.pattern, s.snapped);
            }
        }
    };

    auto commitFinished = [&](FinishedStrokeData&& fin) {
        CommittedStroke s;
        s.raw       = rawSensorHud.GetActiveOrLastStroke();
        s.modeled   = !fin.rawModeledPoints.empty() ? std::move(fin.rawModeledPoints) : fin.modeledPoints;
        s.points    = std::move(fin.modeledPoints);
        s.outline   = std::move(fin.outlinePath);
        s.style     = curStyle();
        s.brush     = curBrush();
        s.brushKind = activeBrush;
        s.zoom      = viewport.zoom;
        s.wantSnap  = snapModeActive() && (snapLatched || (labMode == LabMode::Snapping && snapOnLift));
        if (s.wantSnap) s.snapped = ApplySnapToStroke(s, engine.GetConfig());
        strokes.push_back(std::move(s));
    };

    auto injectRawStroke = [&](std::vector<RawInputSample> raw, bool wantSnap) {
        if (raw.size() < 2 || engine.IsStrokeActive()) return;
        if (!multiStroke) strokes.clear();
        strokeScrubber.Unfreeze();
        CommittedStroke s;
        s.raw       = std::move(raw);
        s.brushKind = activeBrush;
        s.zoom      = viewport.zoom;
        s.wantSnap  = wantSnap;
        rebuildStroke(s);
        rawSensorHud.SetStrokeSamples(s.raw);
        strokes.push_back(std::move(s));
    };

    auto clearCanvas = [&]() {
        engine.CancelStroke();
        strokes.clear();
        liveGpuShapes.clear();
        scrubRaw.clear();
        rawSensorHud.ClearSamples();
        strokeScrubber.Unfreeze();
        snapLatched = false;
        snapPreview = ShapeRecognitionResult{};
    };

    auto undoStroke = [&]() {
        if (engine.IsStrokeActive() || strokes.empty()) return;
        strokes.pop_back();
        strokeScrubber.Unfreeze();
        rawSensorHud.SetStrokeSamples(strokes.empty() ? std::vector<RawInputSample>{} : strokes.back().raw);
    };

    auto enterMode = [&](LabMode m) {
        if (labMode == m) return;
        labMode = m;
        snapLatched = false;
        snapPreview = ShapeRecognitionResult{};
        const int idx = static_cast<int>(m);
        if (!modeVisited[idx]) {
            modeVisited[idx] = true;
            switch (m) {
            case LabMode::PenShape:
                renderPath = RenderPath::FilledOutline;  // nib caps are only visible on the vector outline
                break;
            case LabMode::PostProcessing:
                renderPath = RenderPath::CapsuleSdf;
                break;
            case LabMode::Snapping:
                renderPath = RenderPath::CapsuleSdf;
                break;
            case LabMode::Playground:
                renderPath      = RenderPath::CapsuleSdf;
                whiteBackground = true;
                paperStyle      = PaperStyle::Ruled;
                multiStroke     = true;
                break;
            default:
                break;
            }
        }
    };

    auto renderPathCombo = [&]() {
        int rp = static_cast<int>(renderPath);
        const char* items[] = {
            "GPU Capsule SDF (round caps, grain + opacity)",
            "Filled Vector Outline (real nib caps)",
            "Outline Contour Only"
        };
        if (ImGui::Combo("Render Path", &rp, items, IM_ARRAYSIZE(items))) {
            renderPath = static_cast<RenderPath>(rp);
        }
    };

    auto paperControls = [&]() {
        ImGui::Checkbox("White Background", &whiteBackground);
        int ps = static_cast<int>(paperStyle);
        const char* items[] = { "Blank", "Grid", "Ruled Lines" };
        if (ImGui::Combo("Paper", &ps, items, IM_ARRAYSIZE(items))) {
            paperStyle = static_cast<PaperStyle>(ps);
        }
    };

    // Centerline currently under inspection (live stroke, scrubbed slice, or last committed stroke).
    // Resolved on demand so spans never outlive a replay that reallocates stroke buffers.
    auto resolveCenterPts = [&]() -> std::span<const StrokePoint> {
        if (engine.IsStrokeActive()) return std::span<const StrokePoint>(engine.GetLiveModeledPoints());
        if (strokeScrubber.IsFrozen()) return strokeScrubber.GetActiveModeledSlice();
        if (!strokes.empty()) return std::span<const StrokePoint>(strokes.back().modeled);
        return {};
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
                        if (!multiStroke) strokes.clear();
                        strokeScrubber.Unfreeze();
                        snapLatched  = false;
                        snapPreview  = ShapeRecognitionResult{};
                        holdAnchorX  = sample.x;
                        holdAnchorY  = sample.y;
                        holdStartSec = sample.timestampSec;
                        engine.BeginStroke(sample.x, sample.y, sample.pressure, sample.timestampSec,
                                           curStyle(), viewport.zoom,
                                           rawSensorHud.GetCurrentTiltXRad(),
                                           rawSensorHud.GetCurrentTiltYRad());
                    } else if (strokeEvt == 2) {
                        // Pen/Mouse Motion while down -> Append point
                        engine.AppendPoint(sample.x, sample.y, sample.pressure, sample.timestampSec,
                                           rawSensorHud.GetCurrentTiltXRad(),
                                           rawSensorHud.GetCurrentTiltYRad());
                        // Any movement beyond the dwell threshold restarts the hold timer
                        if (!snapLatched) {
                            const float thr = engine.GetConfig().shapeSnapping.movementThresholdMm;
                            if (std::hypot(sample.x - holdAnchorX, sample.y - holdAnchorY) > thr) {
                                holdAnchorX  = sample.x;
                                holdAnchorY  = sample.y;
                                holdStartSec = sample.timestampSec;
                            }
                        }
                    } else if (strokeEvt == 3) {
                        // Pen/Mouse Up -> Finish stroke and commit it to the canvas
                        if (engine.IsStrokeActive()) {
                            FinishedStrokeData fin = engine.FinishStroke();
                            if (!fin.modeledPoints.empty()) {
                                commitFinished(std::move(fin));
                                if (strokeScrubber.IsAutoFreezeOnLift()) {
                                    const auto& l = strokes.back();
                                    strokeScrubber.FreezeStroke(l.raw, l.modeled, l.style, viewport.zoom);
                                }
                            }
                        }
                        snapLatched = false;
                        snapPreview = ShapeRecognitionResult{};
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

        // Hold-to-snap dwell detection (Mode 3 / 4): once the pen rests long enough, latch the recognized shape
        if (engine.IsStrokeActive() && snapModeActive() &&
            config.shapeSnapping.enableHoldToSnap && !snapLatched) {
            const double nowSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
            if (nowSec - holdStartSec >= static_cast<double>(config.shapeSnapping.holdDwellDurationSec)) {
                ShapeRecognitionResult r = ShapeRecognizer::ClassifyAndFit(engine.GetLiveModeledPoints(), config);
                if (r.type != RecognizedShapeType::None && r.syntheticPoints.size() >= 2) {
                    snapLatched = true;
                    snapPreview = std::move(r);
                }
            }
        }

        // Test stroke generators (centered on the visible canvas, right of the control panel)
        const ImVec2 viewCenterWorld = viewport.ScreenToWorld(static_cast<float>(winWidth) * 0.62f,
                                                              static_cast<float>(winHeight) * 0.5f);
        auto makeScribble = [&]() {
            std::vector<RawInputSample> scribble;
            for (int i = 0; i < 260; ++i) {
                const float t = static_cast<float>(i) * 0.045f;
                RawInputSample s{};
                s.x = viewCenterWorld.x + std::cos(t) * (140.0f + 18.0f * t) * 0.55f + std::sin(t * 2.3f) * 60.0f;
                s.y = viewCenterWorld.y + std::sin(t * 1.15f) * 150.0f;
                s.pressure = 0.35f + 0.5f * (0.5f + 0.5f * std::sin(t * 0.8f));
                s.tiltRad = 0.0f;
                s.timestampSec = static_cast<double>(i) * 0.008;
                scribble.push_back(s);
            }
            return scribble;
        };
        auto makeZigZag = [&]() {
            const ImVec2 corners[] = {
                ImVec2(-170.0f, 40.0f), ImVec2(-110.0f, -60.0f), ImVec2(-50.0f, 50.0f), ImVec2(10.0f, -60.0f),
                ImVec2(70.0f, 50.0f), ImVec2(130.0f, -60.0f), ImVec2(170.0f, 20.0f)
            };
            std::vector<RawInputSample> zig;
            double t = 0.0;
            for (size_t c = 0; c + 1 < IM_ARRAYSIZE(corners); ++c) {
                for (int s = 0; s < 18; ++s) {
                    const float u = static_cast<float>(s) / 18.0f;
                    RawInputSample p{};
                    p.x = viewCenterWorld.x + corners[c].x + (corners[c + 1].x - corners[c].x) * u;
                    p.y = viewCenterWorld.y + corners[c].y + (corners[c + 1].y - corners[c].y) * u;
                    p.pressure = 0.7f;
                    p.tiltRad = 0.0f;
                    p.timestampSec = t;
                    t += 1.0 / 120.0;
                    zig.push_back(p);
                }
            }
            return zig;
        };

        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(470.0f, static_cast<float>(winHeight - 24)), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Inking Lab")) {
            if (ImGui::Button("Reset View (1:1)")) {
                viewport = CanvasTransform{};
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Canvas")) {
                clearCanvas();
                dirtyFrames = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Undo [Ctrl+Z]")) {
                undoStroke();
                dirtyFrames = 2;
            }
            ImGui::Text("Zoom: %.2fx   Strokes on canvas: %zu", viewport.zoom, strokes.size());
            ImGui::Checkbox("Keep multiple strokes on canvas", &multiStroke);
            ImGui::Spacing();

            if (ImGui::BeginTabBar("InkLabModeTabs", ImGuiTabBarFlags_None)) {
                // =============================================================
                // MODE 1: Centerline Lab (everything before the vector outline)
                // =============================================================
                if (ImGui::BeginTabItem("1. Centerline")) {
                    enterMode(LabMode::Centerline);
                    ImGui::TextWrapped(
                        "Everything upstream of the vector outline: raw digitizer -> pressure calibration -> "
                        "spring / wobble / Kalman modeler -> width dynamics. The canvas shows raw samples, the "
                        "modeled centerline, the predicted spine and width discs only.");
                    ImGui::Checkbox("Ghost ink fill under centerline (35%)", &ghostFill);
                    if (ImGui::Button("Re-Run Modeler on Captured Strokes")) {
                        replayStrokes();
                        modelerInspector.SetShowModeledCenterline(true);
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset All Settings")) {
                        config = InkingConfig{};
                        applyConfigChange();
                        replayStrokes();
                        dirtyFrames = 2;
                    }

                    if (ImGui::CollapsingHeader("Raw Sensor", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (rawSensorHud.RenderControls(curStyle().baseWidth * 0.5f)) {
                            dirtyFrames = 2;
                        }
                    }

                    if (ImGui::CollapsingHeader("Modeler: Physics, Filters & Prediction", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (modelerInspector.RenderUI(config)) {
                            applyConfigChange();
                            replayStrokes();
                            dirtyFrames = 2;
                        }
                    }



                    if (ImGui::CollapsingHeader("Time-Travel Scrubber")) {
                        const std::vector<StrokePoint>& curModeled =
                            engine.IsStrokeActive()
                                ? engine.GetLiveModeledPoints()
                                : (strokes.empty() ? kNoPoints : strokes.back().modeled);
                        if (strokeScrubber.RenderUI(curStyle(),
                                                    rawSensorHud.GetActiveOrLastStroke(),
                                                    curModeled,
                                                    scrubRaw,
                                                    viewport,
                                                    viewport.zoom)) {
                            if (!scrubRaw.empty()) {
                                rawSensorHud.SetStrokeSamples(scrubRaw);
                            }
                            dirtyFrames = 2;
                        }
                    }
                    ImGui::EndTabItem();
                }

                // =============================================================
                // MODE 2: Pen cap & width lab (vector outline, no raw data)
                // =============================================================
                if (ImGui::BeginTabItem("2. Pen & Width")) {
                    enterMode(LabMode::PenShape);
                    ImGui::TextWrapped(
                        "Vector-outline lab. Raw sensor data is hidden; pick the nib cap, pattern and width "
                        "behavior and see the generated stroke. Caps and patterns are visible with the "
                        "Filled Vector Outline render path.");
                    paperControls();
                    renderPathCombo();
                    ImGui::Checkbox("Show Centerline Overlay", &showCenterline);

                    StrokeStyle& style = curStyle();

                    ImGui::SeparatorText("Nib Cap & Pattern");
                    int capInt = static_cast<int>(style.capType);
                    const char* capNames[] = { "Round", "Chisel (Calligraphy)", "Square (Highlighter)" };
                    if (ImGui::Combo("Cap Type", &capInt, capNames, IM_ARRAYSIZE(capNames))) {
                        style.capType = static_cast<CapType>(capInt);
                        restyleStrokes(true);
                        dirtyFrames = 2;
                    }
                    int patInt = static_cast<int>(style.pattern);
                    const char* patNames[] = { "Solid", "Dashed", "Dotted", "DashDot", "TexturedPencil" };
                    if (ImGui::Combo("Stroke Pattern", &patInt, patNames, IM_ARRAYSIZE(patNames))) {
                        style.pattern = static_cast<StrokePattern>(patInt);
                        restyleStrokes(true);
                        dirtyFrames = 2;
                    }
                    float col4[4] = {
                        style.color.r / 255.0f, style.color.g / 255.0f,
                        style.color.b / 255.0f, style.color.a / 255.0f
                    };
                    if (ImGui::ColorEdit4("Stroke Pigment", col4, ImGuiColorEditFlags_NoInputs)) {
                        style.color = ColorRgba(static_cast<uint8_t>(col4[0] * 255.0f),
                                                static_cast<uint8_t>(col4[1] * 255.0f),
                                                static_cast<uint8_t>(col4[2] * 255.0f),
                                                static_cast<uint8_t>(col4[3] * 255.0f));
                        restyleStrokes(false);
                        dirtyFrames = 2;
                    }

                    if (widthInspector.RenderUI(config, style, resolveCenterPts())) {
                        applyConfigChange();
                        replayStrokes();
                        dirtyFrames = 2;
                    }

                    ImGui::SeparatorText("Outline Post-Processing");
                    bool postDirty = false;
                    postDirty |= ImGui::Checkbox("Stationary Envelopment (ds <= |dr|)",
                                                 &config.postProcessing.stationaryEnvelopment);
                    postDirty |= ImGui::Checkbox("Enable Simplification (RDP)",
                                                 &config.postProcessing.enableSimplification);
                    if (config.postProcessing.enableSimplification) {
                        postDirty |= ImGui::SliderFloat("Simplify tolerance (mm)",
                                                        &config.postProcessing.simplificationToleranceMm,
                                                        0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
                        postDirty |= ImGui::SliderFloat("Simplify max turn (deg)",
                                                        &config.postProcessing.simplificationMaxTurnDeg,
                                                        0.5f, 20.0f, "%.1f");
                        postDirty |= ImGui::SliderFloat("Simplify radius tolerance",
                                                        &config.postProcessing.simplificationRadiusTolerance,
                                                        0.01f, 0.5f, "%.2f");
                    }
                    postDirty |= ImGui::SliderFloat("Joint cap min turn (deg)",
                                                    &config.postProcessing.minTurnAngleForJointCapDeg,
                                                    0.0f, 30.0f, "%.1f");
                    if (postDirty) {
                        applyConfigChange();
                        replayStrokes();
                        dirtyFrames = 2;
                    }

                    ImGui::SeparatorText("Test Strokes");
                    if (ImGui::Button("Inject Zig-Zag (caps + joins)")) {
                        injectRawStroke(makeZigZag(), false);
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Inject Scribble")) {
                        injectRawStroke(makeScribble(), false);
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // =============================================================
                // MODE 3: Post-Processing & Simplification (Before vs After)
                // =============================================================
                if (ImGui::BeginTabItem("3. Post-Processing")) {
                    enterMode(LabMode::PostProcessing);
                    ImGui::TextWrapped(
                        "Interactive Before vs After comparison inspector. Draw freehand curves and examine "
                        "vertex decimation (RDP algorithm) and Gaussian radius smoothing. Ensure tight details, "
                        "inflection turns, and thickness profiles aren't cut or distorted.");
                    paperControls();
                    renderPathCombo();

                    StrokeStyle& style = curStyle();
                    bool postDirty = false;

                    ImGui::SeparatorText("1. Curve Simplification (Ramer-Douglas-Peucker)");
                    postDirty |= ImGui::Checkbox("Enable Simplification (RDP)",
                                                 &config.postProcessing.enableSimplification);
                    if (config.postProcessing.enableSimplification) {
                        postDirty |= ImGui::SliderFloat("Perpendicular Distance Tol (mm)",
                                                        &config.postProcessing.simplificationToleranceMm,
                                                        0.001f, 0.5f, "%.4f mm", ImGuiSliderFlags_Logarithmic);
                        postDirty |= ImGui::SliderFloat("Max Turn Deviation (deg)",
                                                        &config.postProcessing.simplificationMaxTurnDeg,
                                                        0.5f, 30.0f, "%.1f deg");
                        postDirty |= ImGui::SliderFloat("Radius Tolerance Ratio",
                                                        &config.postProcessing.simplificationRadiusTolerance,
                                                        0.01f, 0.5f, "%.2f");
                        ImGui::TextDisabled("Distance keeps chords close; Turn preserves circular arcs; Radius preserves tapers.");
                    }

                    ImGui::SeparatorText("2. Spatial Gaussian Radius Smoothing");
                    postDirty |= ImGui::Checkbox("Enable Gaussian Radius Smoothing",
                                                 &config.dynamics.enableGaussianRadiusSmoothing);
                    if (config.dynamics.enableGaussianRadiusSmoothing) {
                        postDirty |= ImGui::SliderFloat("Kernel Sigma (mm)",
                                                        &config.dynamics.radiusSmoothingKernelSigmaMm,
                                                        0.2f, 15.0f, "%.1f mm");
                        postDirty |= ImGui::SliderInt("Smoothing Passes",
                                                      &config.dynamics.radiusSmoothingPasses,
                                                      1, 5);
                        ImGui::TextDisabled("Filters out high-frequency digitizer pressure jitter into smooth hills.");
                    }

                    ImGui::SeparatorText("3. Tail Tapering");
                    postDirty |= ImGui::SliderFloat("Tail Taper Length (mm)",
                                                    &config.dynamics.tailTaperLengthMm,
                                                    0.0f, 200.0f, "%.1f mm");
                    if (config.dynamics.tailTaperLengthMm > 0.0f) {
                        postDirty |= ImGui::SliderFloat("Tail Taper Power",
                                                        &config.dynamics.tailTaperPower,
                                                        0.2f, 6.0f, "%.2f");
                    }

                    ImGui::SeparatorText("Visual Before vs After Overlays");
                    ImGui::Checkbox("Show BEFORE Centerline (Red Polyline + Dots)", &showBeforePoints);
                    ImGui::Checkbox("Show AFTER Centerline (Green Polyline + Dots)", &showAfterPoints);
                    ImGui::Checkbox("Show BEFORE Radius Discs (Orange)", &showBeforeDiscs);
                    ImGui::Checkbox("Show AFTER Radius Discs (Cyan)", &showAfterDiscs);

                    // Real-time metrics
                    ImGui::SeparatorText("Simplification Telemetry");
                    if (!strokes.empty()) {
                        const auto& lastS = strokes.back();
                        size_t beforeCount = lastS.modeled.size();
                        size_t afterCount  = lastS.points.size();
                        float decimationPct = (beforeCount > 0)
                            ? (100.0f * (1.0f - static_cast<float>(afterCount) / static_cast<float>(beforeCount)))
                            : 0.0f;

                        ImGui::Text("Last Stroke Before: %zu points  -->  After: %zu points", beforeCount, afterCount);
                        ImVec4 decCol = (decimationPct > 60.0f) ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f)
                                                                : ImVec4(0.9f, 0.8f, 0.2f, 1.0f);
                        ImGui::TextColored(decCol, "Decimation Ratio: %.1f%% fewer vertices", decimationPct);
                    } else if (engine.IsStrokeActive()) {
                        ImGui::Text("Active drawing in progress (%zu modeled points)...", engine.GetLiveModeledPoints().size());
                    } else {
                        ImGui::TextDisabled("No stroke on canvas. Draw a stroke or inject test samples.");
                    }

                    if (postDirty) {
                        applyConfigChange();
                        replayStrokes();
                        dirtyFrames = 2;
                    }

                    ImGui::SeparatorText("Test Strokes");
                    if (ImGui::Button("Inject Zig-Zag Curve")) {
                        injectRawStroke(makeZigZag(), false);
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Inject Rapid Scribble")) {
                        injectRawStroke(makeScribble(), false);
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // =============================================================
                // MODE 4: Geometry approximation (shape snapping)
                // =============================================================
                if (ImGui::BeginTabItem("4. Shape Snapping")) {
                    enterMode(LabMode::Snapping);
                    ImGui::TextWrapped(
                        "Draw a line, circle, ellipse, triangle or rectangle and hold the pen still: the "
                        "recognizer latches the fitted shape and the stroke is replaced on release. "
                        "The green overlay always shows what the recognizer would snap to.");
                    paperControls();
                    renderPathCombo();
                    ImGui::Checkbox("Snap on pen-up even without a hold", &snapOnLift);
                    ImGui::Checkbox("Show snap candidate overlay (green)", &showSnapCandidate);
                    ImGui::Checkbox("Show Centerline Overlay", &showCenterline);

                    if (engine.IsStrokeActive() && config.shapeSnapping.enableHoldToSnap) {
                        const double nowSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
                        const float dwell = std::max(0.01f, config.shapeSnapping.holdDwellDurationSec);
                        const float prog = snapLatched
                                               ? 1.0f
                                               : std::clamp(static_cast<float>(nowSec - holdStartSec) / dwell, 0.0f, 1.0f);
                        ImGui::ProgressBar(prog, ImVec2(-1.0f, 0.0f),
                                           snapLatched ? "SNAPPED - release to commit" : "Hold still to snap...");
                    }

                    ImGui::SeparatorText("Test Shapes (hand-drawn style, jittered)");
                    const char* shapeButtons[] = { "Circle", "Rectangle", "Triangle", "Line" };
                    for (int k = 0; k < 4; ++k) {
                        if (k > 0) ImGui::SameLine();
                        if (ImGui::Button(shapeButtons[k])) {
                            injectRawStroke(MakeTestShapeSamples(k, viewCenterWorld.x, viewCenterWorld.y), true);
                            dirtyFrames = 2;
                        }
                    }
                    if (ImGui::Button("Snap Last Stroke Now") && !strokes.empty() && !engine.IsStrokeActive()) {
                        strokes.back().wantSnap = true;
                        rebuildStroke(strokes.back());
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Un-Snap Last Stroke") && !strokes.empty() && !engine.IsStrokeActive()) {
                        strokes.back().wantSnap = false;
                        rebuildStroke(strokes.back());
                        dirtyFrames = 2;
                    }

                    ImGui::SeparatorText("GPU Shape SDF Generator (Hybrid System)");
                    ImGui::TextWrapped("Directly spawns analytic GPU shapes (rectangles, rounded boxes, circles) with sub-pixel anti-aliasing and zero CPU triangle tessellation.");
                    if (ImGui::Button("Add Box (GPU)")) {
                        liveGpuShapes.push_back(ShapeBuilder::MakeRectangle(viewCenterWorld.x - 30.0f, viewCenterWorld.y, 50.0f, 35.0f,
                                                                            0x803366CCu, 0xFFFFFFFFu, 1.5f));
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Rounded Box (GPU)")) {
                        liveGpuShapes.push_back(ShapeBuilder::MakeRoundedRectangle(viewCenterWorld.x + 30.0f, viewCenterWorld.y, 50.0f, 35.0f, 8.0f,
                                                                                   0x8022AA66u, 0xFFFFFFFFu, 1.5f));
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Circle (GPU)")) {
                        liveGpuShapes.push_back(ShapeBuilder::MakeCircle(viewCenterWorld.x, viewCenterWorld.y + 45.0f, 20.0f,
                                                                         0x80CC4422u, 0xFFFFFFFFu, 1.5f));
                        dirtyFrames = 2;
                    }

                    const ShapeRecognitionResult& lastRec = geometryInspector.GetLastRecognitionResult();
                    const bool canTraceSnap = (lastRec.type != RecognizedShapeType::None && !lastRec.syntheticPoints.empty());
                    if (ImGui::Button("Trace Snapped Shape with GPU SDF") && canTraceSnap) {
                        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
                        for (const auto& pt : lastRec.syntheticPoints) {
                            const float px = static_cast<float>(pt.x);
                            const float py = static_cast<float>(pt.y);
                            minX = std::min(minX, px);
                            maxX = std::max(maxX, px);
                            minY = std::min(minY, py);
                            maxY = std::max(maxY, py);
                        }
                        const float cx = 0.5f * (minX + maxX);
                        const float cy = 0.5f * (minY + maxY);
                        const float w  = std::max(1.0f, maxX - minX);
                        const float h  = std::max(1.0f, maxY - minY);
                        const uint32_t fillCol = 0x6000FFAAu;
                        const uint32_t strokeCol = 0xFFFFFFFFu;

                        if (lastRec.type == RecognizedShapeType::Circle) {
                            liveGpuShapes.push_back(ShapeBuilder::MakeCircle(cx, cy, 0.5f * std::max(w, h), fillCol, strokeCol, 1.5f));
                        } else if (lastRec.type == RecognizedShapeType::Rectangle || lastRec.type == RecognizedShapeType::Square) {
                            liveGpuShapes.push_back(ShapeBuilder::MakeRoundedRectangle(cx, cy, w, h, 2.0f, fillCol, strokeCol, 1.5f));
                        } else if (lastRec.type == RecognizedShapeType::Ellipse) {
                            liveGpuShapes.push_back(ShapeBuilder::MakeEllipse(cx, cy, 0.5f * w, 0.5f * h, fillCol, strokeCol, 1.5f));
                        } else {
                            liveGpuShapes.push_back(ShapeBuilder::MakeRectangle(cx, cy, w, h, fillCol, strokeCol, 1.5f));
                        }
                        dirtyFrames = 2;
                    }
                    if (!canTraceSnap) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(Draw and hold a shape to enable trace button)");
                    }
                    if (!liveGpuShapes.empty()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear GPU Shapes")) {
                            liveGpuShapes.clear();
                            dirtyFrames = 2;
                        }
                    }

                    if (geometryInspector.RenderSnappingUI(config)) {
                        applyConfigChange();
                        replayStrokes();
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                // =============================================================
                // MODE 5: Playground (finished product test bench)
                // =============================================================
                if (ImGui::BeginTabItem("5. Playground")) {
                    enterMode(LabMode::Playground);
                    ImGui::TextWrapped(
                        "Finished-product bench: write several strokes, mix pen / pencil / highlighter, check "
                        "pencil darkness build-up and tilt shading, and test the highlighter over text. "
                        "Hold still after a shape to snap it (tune in Mode 3).");
                    paperControls();
                    renderPathCombo();
                    ImGui::Checkbox("Show Centerline Overlay", &showCenterline);

                    ImGui::SeparatorText("Brush");
                    const char* brushNames[] = { "Pen", "Pencil", "Highlighter" };
                    for (int b = 0; b < 3; ++b) {
                        if (b > 0) ImGui::SameLine();
                        if (ImGui::RadioButton(brushNames[b], activeBrush == b)) {
                            activeBrush = b;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset Brush")) {
                        InitBrushSlot(brushSlots[activeBrush], activeBrush);
                        replayStrokes();
                        dirtyFrames = 2;
                    }

                    StrokeStyle&  style = curStyle();
                    CapsuleBrush& brush = curBrush();
                    bool geoDirty = false;   // changes the modeled centerline / widths
                    bool outDirty = false;   // changes only the vector outline
                    bool lookDirty = false;  // changes only color / opacity / grain
                    bool cfgDirty = false;   // global evaluator settings

                    geoDirty |= ImGui::SliderFloat("Width", &style.baseWidth, 1.0f, 60.0f, "%.1f");
                    geoDirty |= ImGui::Checkbox("Use Pressure", &style.usePressure);
                    float col4[4] = {
                        style.color.r / 255.0f, style.color.g / 255.0f,
                        style.color.b / 255.0f, style.color.a / 255.0f
                    };
                    if (ImGui::ColorEdit4("Pigment", col4, ImGuiColorEditFlags_NoInputs)) {
                        style.color = ColorRgba(static_cast<uint8_t>(col4[0] * 255.0f),
                                                static_cast<uint8_t>(col4[1] * 255.0f),
                                                static_cast<uint8_t>(col4[2] * 255.0f),
                                                static_cast<uint8_t>(col4[3] * 255.0f));
                        lookDirty = true;
                    }
                    lookDirty |= ImGui::SliderFloat("Opacity", &brush.opacity, 0.05f, 1.0f, "%.2f");
                    lookDirty |= ImGui::Checkbox("Union Self-Overlap (no double-darkening)", &brush.unionOverlap);

                    int capInt = static_cast<int>(style.capType);
                    const char* capNames[] = { "Round", "Chisel (Calligraphy)", "Square (Highlighter)" };
                    if (ImGui::Combo("Cap Type", &capInt, capNames, IM_ARRAYSIZE(capNames))) {
                        style.capType = static_cast<CapType>(capInt);
                        outDirty = true;
                    }

                    if (activeBrush == kBrushPen) {
                        geoDirty |= ImGui::Checkbox("Velocity Thinning (fountain pen)", &style.useVelocity);
                    } else if (activeBrush == kBrushPencil) {
                        ImGui::SeparatorText("Pencil Tuning");
                        lookDirty |= ImGui::SliderFloat("Grain Strength", &brush.grain, 0.0f, 1.0f, "%.2f");
                        lookDirty |= ImGui::SliderFloat("Grain Scale (cells/unit)", &brush.grainScale, 0.1f, 3.0f, "%.2f");
                        geoDirty |= ImGui::Checkbox("Tilt Shading (wider when tilted)", &style.useTilt);
                        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
                        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
                        cfgDirty |= ImGui::SliderFloat("Max tilt width", &config.dynamics.maxTiltWidthMultiplier,
                                                       1.0f, 6.0f, "%.2fx");
                        float deadDeg = config.dynamics.tiltDeadzoneRad * kRadToDeg;
                        if (ImGui::SliderFloat("Tilt deadzone", &deadDeg, 0.0f, 45.0f, "%.1f deg")) {
                            config.dynamics.tiltDeadzoneRad = deadDeg * kDegToRad;
                            cfgDirty = true;
                        }
                        float maxDeg = config.dynamics.tiltMaxAngleRad * kRadToDeg;
                        if (ImGui::SliderFloat("Tilt full-shade angle", &maxDeg, 20.0f, 89.0f, "%.1f deg")) {
                            config.dynamics.tiltMaxAngleRad = maxDeg * kDegToRad;
                            cfgDirty = true;
                        }
                        cfgDirty |= ImGui::SliderFloat("Pressure gamma", &config.calibration.pressureGamma,
                                                       0.25f, 3.0f, "%.2f");
                        cfgDirty |= ImGui::SliderFloat("Min width ratio", &config.dynamics.minWidthMultiplier,
                                                       0.05f, 1.0f, "%.2f");
                        cfgDirty |= ImGui::SliderFloat("Max width ratio", &config.dynamics.maxWidthMultiplier,
                                                       0.5f, 3.0f, "%.2f");
                        ImGui::TextDisabled("Tilt needs a tilt-capable stylus. Grain only shows on the Capsule SDF path.");
                    } else {
                        ImGui::TextDisabled("Highlighter: constant width, translucent. Turn Union OFF to see the "
                                            "double-darkening artifact; Square caps show on the Filled Outline path.");
                    }

                    if (cfgDirty) {
                        applyConfigChange();
                        replayStrokes();
                        dirtyFrames = 2;
                    } else if (geoDirty) {
                        replayStrokes();
                        dirtyFrames = 2;
                    } else if (outDirty) {
                        restyleStrokes(true);
                        dirtyFrames = 2;
                    } else if (lookDirty) {
                        restyleStrokes(false);
                        dirtyFrames = 2;
                    }

                    ImGui::SeparatorText("Test Strokes");
                    if (ImGui::Button("Inject Scribble (self-overlap test)")) {
                        injectRawStroke(makeScribble(), false);
                        dirtyFrames = 2;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Inject Zig-Zag")) {
                        injectRawStroke(makeZigZag(), false);
                        dirtyFrames = 2;
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        // Undo hotkey
        if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            undoStroke();
            dirtyFrames = 2;
        }

        // Resolve what is on screen now that all UI-driven replays have settled
        const std::span<const StrokePoint> centerPts = resolveCenterPts();
        std::span<const StrokePoint> predictedPts{};
        if (engine.IsStrokeActive()) {
            predictedPts = engine.GetPredictedPoints();
        }

        // Mode 3: live recognition feedback (what would the recognizer snap to right now?)
        if (labMode == LabMode::Snapping) {
            if (engine.IsStrokeActive()) {
                geometryInspector.UpdateShapeRecognition(engine.GetLiveModeledPoints(), config);
            } else if (!strokes.empty()) {
                geometryInspector.UpdateShapeRecognition(strokes.back().modeled, config);
            } else {
                geometryInspector.UpdateShapeRecognition({}, config);
            }
        }

        // =====================================================================
        // Canvas overlays on the background ImDrawList (drawn after the GL ink)
        // =====================================================================
        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();

        if (labMode == LabMode::Centerline) {
            // Mode 1: raw sensor + modeler + width diagnostics
            widthInspector.RenderOverlays(bgDrawList, centerPts, viewport);
            modelerInspector.RenderOverlays(bgDrawList, rawSensorHud.GetActiveOrLastStroke(),
                                            centerPts, predictedPts, viewport);
            strokeScrubber.RenderCursorHighlight(bgDrawList, viewport);
            rawSensorHud.Render(bgDrawList, viewport);
        } else if (labMode == LabMode::PostProcessing) {
            // Mode 3: Interactive Before vs After Simplification & Smoothing
            for (const auto& s : strokes) {
                // 1. BEFORE (raw modeled points directly from spring-mass ODE)
                if (showBeforePoints && s.modeled.size() >= 2) {
                    const ImU32 beforeLineCol = IM_COL32(255, 60, 60, 200);  // Bright Red line
                    const ImU32 beforeDotCol  = IM_COL32(255, 100, 100, 255); // Red dots
                    DrawWorldPolyline(bgDrawList, s.modeled, viewport, beforeLineCol, 1.5f);
                    for (const auto& pt : s.modeled) {
                        ImVec2 sp = viewport.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                        bgDrawList->AddCircleFilled(sp, 2.5f, beforeDotCol, 10);
                    }
                }

                // 2. AFTER (simplified & smoothed points)
                if (showAfterPoints && s.points.size() >= 2) {
                    const ImU32 afterLineCol = IM_COL32(40, 255, 120, 220);  // Vivid Green/Cyan line
                    const ImU32 afterDotCol  = IM_COL32(80, 255, 180, 255);  // Green/Cyan dots
                    DrawWorldPolyline(bgDrawList, s.points, viewport, afterLineCol, 2.2f);
                    for (const auto& pt : s.points) {
                        ImVec2 sp = viewport.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                        bgDrawList->AddCircleFilled(sp, 3.5f, afterDotCol, 12);
                    }
                }

                // 3. BEFORE Discs (Orange hollow rings)
                if (showBeforeDiscs) {
                    const ImU32 beforeDiscCol = IM_COL32(255, 140, 30, 150);
                    for (const auto& pt : s.modeled) {
                        ImVec2 c = viewport.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                        float rPx = std::max(1.0f, pt.r * viewport.zoom);
                        bgDrawList->AddCircle(c, rPx, beforeDiscCol, 20, 1.0f);
                    }
                }

                // 4. AFTER Discs (Cyan hollow rings)
                if (showAfterDiscs) {
                    const ImU32 afterDiscCol = IM_COL32(0, 230, 255, 180);
                    for (const auto& pt : s.points) {
                        ImVec2 c = viewport.WorldToScreen(static_cast<float>(pt.x), static_cast<float>(pt.y));
                        float rPx = std::max(1.0f, pt.r * viewport.zoom);
                        bgDrawList->AddCircle(c, rPx, afterDiscCol, 24, 1.2f);
                    }
                }
            }

            // Live in-flight stroke overlay while dragging in Mode 3
            if (engine.IsStrokeActive()) {
                const auto& livePts = engine.GetLiveModeledPoints();
                if (showBeforePoints && livePts.size() >= 2) {
                    DrawWorldPolyline(bgDrawList, livePts, viewport, IM_COL32(255, 80, 80, 220), 1.5f);
                }
            }
        } else {
            static const InkMesh kEmptyMesh{};

            // Outline contour render path (vector outline edges instead of a filled body)
            if (renderPath == RenderPath::OutlineContour) {
                geometryInspector.SetEnabled(true);
                geometryInspector.SetViewMode(GeometryViewMode::CpuOutlinePolygon);
                for (const auto& s : strokes) {
                    geometryInspector.RenderCanvasOverlays(bgDrawList, s.points, s.outline, kEmptyMesh, viewport);
                }
                if (engine.IsStrokeActive()) {
                    const StrokeStyle& st = curStyle();
                    const InkPath liveOutline = InkEngine::BuildStrokeOutline(
                        engine.GetLiveModeledPoints(), st.capType, st.pattern);
                    geometryInspector.RenderCanvasOverlays(bgDrawList, engine.GetLiveModeledPoints(),
                                                           liveOutline, kEmptyMesh, viewport);
                }
            }

            // Optional centerline overlay on top of every stroke
            if (showCenterline) {
                const ImU32 centerlineCol = whiteBackground ? IM_COL32(225, 35, 120, 255)
                                                            : IM_COL32(255, 235, 40, 240);
                for (const auto& s : strokes) {
                    DrawWorldPolyline(bgDrawList, s.points, viewport, centerlineCol, 1.5f);
                }
                if (engine.IsStrokeActive()) {
                    DrawWorldPolyline(bgDrawList, engine.GetLiveModeledPoints(), viewport, centerlineCol, 1.5f);
                }
            }

            // Mode 3: snap candidate overlay
            if (labMode == LabMode::Snapping && showSnapCandidate) {
                const ShapeRecognitionResult& rec = geometryInspector.GetLastRecognitionResult();
                if (rec.type != RecognizedShapeType::None && rec.syntheticPoints.size() >= 2) {
                    DrawWorldPolyline(bgDrawList, rec.syntheticPoints, viewport, IM_COL32(0, 255, 180, 230), 2.5f);
                }
            }
        }

        // =====================================================================
        // OpenGL render pass: background -> paper -> ink -> ImGui
        // =====================================================================
        ImGui::Render();
        glViewport(0, 0, fbWidth, fbHeight);
        if (whiteBackground) {
            glClearColor(0.98f, 0.98f, 0.97f, 1.0f);
        } else {
            glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        }
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // Paper (grid / ruled lines) drawn beneath the ink
        std::vector<std::vector<PathPoint>> paperContours;
        BuildPaperContours(paperStyle, viewport, winWidth, winHeight, paperContours);
        if (!paperContours.empty()) {
            const ColorRgba lineCol = whiteBackground ? ColorRgba(214, 222, 238, 255)
                                                      : ColorRgba(34, 38, 46, 255);
            glMeshRenderer.FillContours(paperContours, PackGpuColor(lineCol), viewport, winWidth, winHeight);
        }

        std::vector<std::vector<PathPoint>> strokeContours;
        auto drawInk = [&](std::span<const StrokePoint> pts, const InkPath* outline,
                           const StrokeStyle& st, const CapsuleBrush& br) {
            if (pts.empty()) return;
            if (renderPath == RenderPath::CapsuleSdf) {
                glCapsuleRenderer.RenderStroke(pts, PackPremultipliedGpuColor(st.color),
                                               viewport, winWidth, winHeight, br);
            } else if (renderPath == RenderPath::FilledOutline && outline && !outline->empty()) {
                FlattenOutline(*outline, strokeContours);
                glMeshRenderer.FillContours(strokeContours, PackGpuColor(st.color, br.opacity),
                                            viewport, winWidth, winHeight);
            }
        };

        if (labMode == LabMode::Centerline) {
            // Mode 1: optional translucent ghost of the ink under the centerline diagnostics
            if (ghostFill) {
                const CapsuleBrush ghost{0.35f, 0.0f, 0.6f, true};
                const StrokeStyle& st = curStyle();
                if (engine.IsStrokeActive() || strokeScrubber.IsFrozen()) {
                    if (!centerPts.empty()) {
                        glCapsuleRenderer.RenderStroke(centerPts, PackPremultipliedGpuColor(st.color),
                                                       viewport, winWidth, winHeight, ghost);
                    }
                } else {
                    for (const auto& s : strokes) {
                        glCapsuleRenderer.RenderStroke(s.modeled, PackPremultipliedGpuColor(s.style.color),
                                                       viewport, winWidth, winHeight, ghost);
                    }
                }
            }
        } else {
            for (const auto& s : strokes) {
                drawInk(s.points, &s.outline, s.style, s.brush);
            }

            if (engine.IsStrokeActive()) {
                const StrokeStyle&  st = curStyle();
                const CapsuleBrush& br = curBrush();
                if (snapLatched && snapPreview.syntheticPoints.size() >= 2) {
                    // Hold-to-snap latched: show the fitted shape instead of the freehand stroke
                    InkPath snapOutline;
                    if (renderPath == RenderPath::FilledOutline) {
                        snapOutline = InkEngine::BuildStrokeOutline(snapPreview.syntheticPoints,
                                                                    st.capType, st.pattern, true);
                    }
                    drawInk(snapPreview.syntheticPoints, &snapOutline, st, br);
                } else if (renderPath == RenderPath::FilledOutline) {
                    // Live body + predicted tip as one outline so the join stays seamless
                    std::vector<StrokePoint> combined(engine.GetLiveModeledPoints());
                    if (config.prediction.enable && predictedPts.size() > 1) {
                        combined.insert(combined.end(), predictedPts.begin() + 1, predictedPts.end());
                    }
                    const InkPath liveOutline = InkEngine::BuildStrokeOutline(combined, st.capType, st.pattern);
                    drawInk(combined, &liveOutline, st, br);
                } else {
                    drawInk(engine.GetLiveModeledPoints(), nullptr, st, br);
                    if (renderPath == RenderPath::CapsuleSdf && config.prediction.enable && !predictedPts.empty()) {
                        glCapsuleRenderer.RenderStroke(predictedPts, PackPremultipliedGpuColor(st.color),
                                                       viewport, winWidth, winHeight, br);
                    }
                }
            }
        }

        // Render GPU SDF shapes if any exist
        if (!liveGpuShapes.empty()) {
            glShapeRenderer.UploadShapes(liveGpuShapes);
            glShapeRenderer.Render(viewport, winWidth, winHeight);
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    glShapeRenderer.Shutdown();
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
