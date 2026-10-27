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
#include <iostream>

#include "folio_sketch/folio_sketch.hpp"
#include "debug/raw_sensor_layer.hpp"
#include "debug/modeler_inspector.hpp"
#include "debug/width_inspector.hpp"
#include "debug/geometry_inspector.hpp"
#include "debug/stroke_scrubber.hpp"

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

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
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

        // =====================================================================
        // ImGui Diagnostic Control Dock (Left Panel)
        // =====================================================================
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

            // -----------------------------------------------------------------
            // Step 1: Raw Sensor HUD & Input Capture
            // -----------------------------------------------------------------
            if (ImGui::CollapsingHeader("Step 1: Raw Sensor HUD & Input Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (rawSensorHud.RenderControls(strokeStyle.baseWidth * 0.5f)) {
                    dirtyFrames = 2;
                }
            }

            // -----------------------------------------------------------------
            // Step 2: Google Stroke Modeler Inspector
            // -----------------------------------------------------------------
            if (ImGui::CollapsingHeader("Step 2: Google Stroke Modeler Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (modelerInspector.RenderUI(config)) {
                    engine.UpdatePhysicsParams();
                    // Re-run raw samples through Google Stroke Modeler if we have captured raw samples
                    if (!lastRawSamples.empty() && !engine.IsStrokeActive()) {
                        ReplayRawSamplesThroughEngine(engine, lastRawSamples, strokeStyle, viewport.zoom, lastFinishedStroke);
                        if (strokeScrubber.IsFrozen()) {
                            strokeScrubber.FreezeStroke(lastRawSamples, lastFinishedStroke.modeledPoints,
                                                        strokeStyle, viewport.zoom);
                        }
                    }
                    dirtyFrames = 2;
                }
            }

            // -----------------------------------------------------------------
            // Step 3: Width, Pressure & Velocity Evaluator Panel
            // -----------------------------------------------------------------
            std::span<const StrokePoint> ptsForPlot =
                strokeScrubber.IsFrozen()
                    ? strokeScrubber.GetActiveModeledSlice()
                    : (engine.IsStrokeActive()
                           ? std::span<const StrokePoint>(engine.GetLiveModeledPoints())
                           : std::span<const StrokePoint>(lastFinishedStroke.modeledPoints));

            if (ImGui::CollapsingHeader("Step 3: Width, Pressure & Velocity Evaluator", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (widthInspector.RenderUI(config, strokeStyle, ptsForPlot)) {
                    engine.SetConfig(config);
                    if (strokeScrubber.IsFrozen()) {
                        WidthInspector::ReevaluateRadii(strokeScrubber.GetMutableFrozenPoints(),
                                                        config, strokeStyle, viewport.zoom);
                        strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                    } else if (!lastFinishedStroke.modeledPoints.empty()) {
                        WidthInspector::ReevaluateRadii(lastFinishedStroke.modeledPoints,
                                                        config, strokeStyle, viewport.zoom);
                        lastFinishedStroke.outlinePath = InkEngine::BuildStrokeOutline(
                            lastFinishedStroke.modeledPoints, strokeStyle.capType, strokeStyle.pattern);
                        InkEngine::BuildStrokeMesh(
                            lastFinishedStroke.modeledPoints, strokeStyle.capType,
                            PackGpuColor(strokeStyle.color), lastFinishedStroke.mesh,
                            viewport.zoom, strokeStyle.pattern);
                    }
                    dirtyFrames = 2;
                }
            }

            // -----------------------------------------------------------------
            // Step 4: Geometry Tessellator vs. Outline Polygon
            // -----------------------------------------------------------------
            if (ImGui::CollapsingHeader("Step 4: Geometry Tessellator vs. Outline Polygon", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (geometryInspector.RenderUI(config, strokeStyle)) {
                    if (strokeScrubber.IsFrozen()) {
                        strokeScrubber.Retessellate(strokeStyle, viewport.zoom);
                    } else if (!lastFinishedStroke.modeledPoints.empty()) {
                        lastFinishedStroke.outlinePath = InkEngine::BuildStrokeOutline(
                            lastFinishedStroke.modeledPoints, strokeStyle.capType, strokeStyle.pattern);
                        InkEngine::BuildStrokeMesh(
                            lastFinishedStroke.modeledPoints, strokeStyle.capType,
                            PackGpuColor(strokeStyle.color), lastFinishedStroke.mesh,
                            viewport.zoom, strokeStyle.pattern);
                    }
                    dirtyFrames = 2;
                }
            }

            // -----------------------------------------------------------------
            // Step 5: Time-Travel Point Scrubber
            // -----------------------------------------------------------------
            if (ImGui::CollapsingHeader("Step 5: Time-Travel Point Scrubber", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            }
        }
        ImGui::End();

        // Resolve active geometry to display and analyze
        if (engine.IsStrokeActive()) {
            activeMesh = &engine.GetLiveMesh();
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
        // OpenGL Render Pass (Background Clear -> Solid GPU InkMesh -> ImGui)
        // =====================================================================
        ImGui::Render();
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        if (activeMesh && geometryInspector.ShouldRenderSolidGpuMesh()) {
            glMeshRenderer.RenderMesh(*activeMesh, viewport, winWidth, winHeight);
            if (engine.IsStrokeActive() && config.google_enable_prediction) {
                glMeshRenderer.RenderMesh(engine.GetPredictedMesh(), viewport, winWidth, winHeight);
            }
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    glMeshRenderer.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
