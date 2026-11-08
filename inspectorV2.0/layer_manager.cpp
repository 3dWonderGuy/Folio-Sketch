/**
 * =========================================================================================
 * @file inspectorV2.0/layer_manager.cpp
 * @brief Implementation of Unified Dual-Layer Manager (Live Inking + Standard Canvas)
 * =========================================================================================
 */

#include "inspectorV2.0/layer_manager.hpp"
#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/testbed/testbed_samples.hpp"
#include "folio_backends/common/display_surface_transform.hpp"
#include <cmath>
#include <algorithm>
#include <imgui_internal.h>

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#endif
#include <backends/imgui_impl_opengl3.h>

namespace Folio::V2 {

LayerManager::LayerManager() {
    m_activeStyle.baseWidth = 10.0f;
    m_activeStyle.color     = Folio::ColorRgba(0, 115, 255, 255); // Electric Blue
    m_activeBrush.opacity   = 1.0f;

    InitInputRouting();
}

LayerManager::~LayerManager() {
    ShutdownImGui();
    ShutdownDocumentFbo();
    if (m_glInitialized) {
        m_capsuleRenderer.Shutdown();
        m_glInitialized = false;
    }
}

void LayerManager::InitInputRouting() {
    auto updateTelemetry = [this](const PointerPacket& pkt) {
        m_telemetry.screenX = pkt.x;
        m_telemetry.screenY = pkt.y;
        m_telemetry.worldX = (pkt.x - m_canvasState.panX) / m_canvasState.zoom;
        m_telemetry.worldY = (pkt.y - m_canvasState.panY) / m_canvasState.zoom;
        m_telemetry.pressure = pkt.pressure;
        m_telemetry.tiltX = pkt.tiltX;
        m_telemetry.tiltY = pkt.tiltY;
        m_telemetry.timestampSec = pkt.timestampSec;
        m_telemetry.isHoverOrActive = (pkt.phase != InputPhase::Up && pkt.phase != InputPhase::Cancel);

        if (m_lastPacketTimeSec > 0.0 && pkt.timestampSec > m_lastPacketTimeSec) {
            const double dt = pkt.timestampSec - m_lastPacketTimeSec;
            if (dt > 1e-5 && dt < 1.0) {
                const float instantHz = static_cast<float>(1.0 / dt);
                m_telemetry.sampleRateHz = (m_telemetry.sampleRateHz <= 0.0f)
                    ? instantHz
                    : (m_telemetry.sampleRateHz * 0.85f + instantHz * 0.15f);
                const float latency = static_cast<float>(dt * 1000.0);
                m_telemetry.inputLatencyMs = (m_telemetry.inputLatencyMs <= 0.0f)
                    ? latency
                    : (m_telemetry.inputLatencyMs * 0.85f + latency * 0.15f);
            }
        }
        m_lastPacketTimeSec = pkt.timestampSec;
    };

    // 1. Live Inking Fast Path (Bypass System)
    m_inputManager.onInkBegin = [this, updateTelemetry](const PointerPacket& pkt) {
        updateTelemetry(pkt);
        m_isStrokeActive = true;
        m_wetUploadedModeledCount = 0;
        m_prevPredictionDirtyRect.valid = false;
        if (m_isTestbedMode) {
            m_documentStrokes.clear();
            m_docFboValid = false; // Force clean FBO bake for single-stroke testbed
        }
        m_lastRawSamples.clear();
        m_lastX = pkt.x;
        m_lastY = pkt.y;
        m_lastPressure = pkt.pressure;

        const float worldX = (pkt.x - m_canvasState.panX) / m_canvasState.zoom;
        const float worldY = (pkt.y - m_canvasState.panY) / m_canvasState.zoom;

        Folio::RawInputSample sample{};
        sample.x = worldX;
        sample.y = worldY;
        sample.pressure = pkt.pressure;
        sample.tiltX = pkt.tiltX;
        sample.tiltY = pkt.tiltY;
        sample.timestampSec = pkt.timestampSec;
        sample.timeNs = static_cast<int64_t>(pkt.timestampSec * 1e9);
        m_lastRawSamples.push_back(sample);

        m_liveEngine.BeginStroke(
            worldX, worldY, pkt.pressure, pkt.timestampSec,
            m_activeStyle, m_canvasState.zoom,
            pkt.tiltX, pkt.tiltY
        );
    };

    m_inputManager.onInkMove = [this, updateTelemetry](const PointerPacket& pkt) {
        updateTelemetry(pkt);
        m_lastX = pkt.x;
        m_lastY = pkt.y;
        m_lastPressure = pkt.pressure;

        const float worldX = (pkt.x - m_canvasState.panX) / m_canvasState.zoom;
        const float worldY = (pkt.y - m_canvasState.panY) / m_canvasState.zoom;

        Folio::RawInputSample sample{};
        sample.x = worldX;
        sample.y = worldY;
        sample.pressure = pkt.pressure;
        sample.tiltX = pkt.tiltX;
        sample.tiltY = pkt.tiltY;
        sample.timestampSec = pkt.timestampSec;
        sample.timeNs = static_cast<int64_t>(pkt.timestampSec * 1e9);
        m_lastRawSamples.push_back(sample);

        m_liveEngine.AppendPoint(
            worldX, worldY, pkt.pressure, pkt.timestampSec,
            pkt.tiltX, pkt.tiltY
        );
    };

    m_inputManager.onInkEnd = [this, updateTelemetry](const PointerPacket& pkt) {
        updateTelemetry(pkt);
        m_lastX = pkt.x;
        m_lastY = pkt.y;
        m_lastPressure = pkt.pressure;

        if (m_isStrokeActive) {
            Folio::FinishedStrokeData fin = m_liveEngine.FinishStroke();
            m_lastRawModeledPoints = fin.rawModeledPoints;
            m_lastFinishedPoints = fin.modeledPoints;
            if (m_isTestbedMode) {
                m_documentStrokes.clear();
            }
            if (!fin.modeledPoints.empty()) {
                DocumentStroke stroke;
                stroke.points = std::move(fin.modeledPoints);
                stroke.color  = Folio::PackPremultipliedGpuColor(m_activeStyle.color);
                stroke.brush  = m_activeBrush;
                m_documentStrokes.push_back(std::move(stroke));
            } else if (m_lastX > 0.0f) {
                const float worldX = (m_lastX - m_canvasState.panX) / m_canvasState.zoom;
                const float worldY = (m_lastY - m_canvasState.panY) / m_canvasState.zoom;
                DocumentStroke stroke;
                stroke.points.emplace_back(worldX, worldY, m_activeStyle.baseWidth * 0.5f, m_lastPressure);
                stroke.color  = Folio::PackPremultipliedGpuColor(m_activeStyle.color);
                stroke.brush  = m_activeBrush;
                m_documentStrokes.push_back(std::move(stroke));
            }

            m_isStrokeActive = false;

            if (onDocumentDirty) {
                onDocumentDirty();
            }
        }
    };

    // 2. Touch Navigation (Translation / Pan & Zoom around focal centroid)
    m_inputManager.onCanvasTranslate = [this](float deltaX, float deltaY) {
        m_canvasState.panX += deltaX;
        m_canvasState.panY += deltaY;

        if (onDocumentDirty) {
            onDocumentDirty();
        }
    };

    m_inputManager.onCanvasZoom = [this](float focalX, float focalY, float zoomFactor) {
        const float oldZoom = m_canvasState.zoom;
        const float newZoom = std::clamp(oldZoom * zoomFactor, 0.1f, 10.0f);
        if (std::abs(newZoom - oldZoom) < 1e-4f) {
            return;
        }

        m_canvasState.panX = focalX - (focalX - m_canvasState.panX) * (newZoom / oldZoom);
        m_canvasState.panY = focalY - (focalY - m_canvasState.panY) * (newZoom / oldZoom);
        m_canvasState.zoom = newZoom;

        if (onDocumentDirty) {
            onDocumentDirty();
        }
    };
}

void LayerManager::ProcessPointer(const PointerPacket& packet) {
    if (m_inspectorGui.simulatedLatencyMs > 0.0f) {
        auto now = std::chrono::steady_clock::now();
        auto releaseTime = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float, std::milli>(m_inspectorGui.simulatedLatencyMs)
        );
        m_delayedPackets.push_back({ packet, releaseTime });
        FlushDelayedPackets();
        return;
    }

    // Flush any leftover packets before processing immediate packet
    FlushDelayedPackets();
    m_inputManager.ProcessPacket(packet);
}

bool LayerManager::FlushDelayedPackets() {
    if (m_delayedPackets.empty()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    bool processedAny = false;

    // If simulated latency was reset to 0, flush everything immediately
    const bool flushAll = (m_inspectorGui.simulatedLatencyMs <= 0.0f);

    while (!m_delayedPackets.empty()) {
        if (!flushAll && m_delayedPackets.front().releaseTime > now) {
            break;
        }
        auto pkt = m_delayedPackets.front().packet;
        m_delayedPackets.pop_front();
        m_inputManager.ProcessPacket(pkt);
        processedAny = true;
    }

    return processedAny;
}

bool LayerManager::IsLiveInkingActive() const noexcept {
    return m_isStrokeActive;
}

void LayerManager::SetActiveBrush(const Folio::StrokeStyle& style, const Folio::Backends::CapsuleBrush& brush) {
    m_activeStyle = style;
    m_activeBrush = brush;
}

void LayerManager::EnsureGl() {
    if (!m_glInitialized) {
        m_capsuleRenderer.Init();
        m_glInitialized = true;
    }
}

void LayerManager::EnsureImGui(int vpWidth, int vpHeight, bool isSdlHost) {
    if (!m_imguiInitialized && ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
#if defined(__ANDROID__)
        if (io.BackendRendererUserData == nullptr) {
            ImGui_ImplOpenGL3_Init("#version 300 es");
        }
        io.FontGlobalScale = (vpWidth > 2000 || vpHeight > 2000) ? 2.2f : 1.4f;
#else
        if (!isSdlHost && io.BackendRendererUserData == nullptr) {
            ImGui_ImplOpenGL3_Init("#version 330 core");
        }
#endif
        ImGui::StyleColorsDark();
        m_imguiInitialized = true;
    } else {
        m_imguiInitialized = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    // Do not overwrite DisplaySize on SDL3 host because ImGui_ImplSDL3_NewFrame manages window vs fb scale
    if (!isSdlHost && vpWidth > 0 && vpHeight > 0) {
        io.DisplaySize = ImVec2(static_cast<float>(vpWidth), static_cast<float>(vpHeight));
    }

    if (!isSdlHost) {
        auto now = std::chrono::steady_clock::now();
        if (m_lastImGuiFrameTime.time_since_epoch().count() > 0) {
            float dt = std::chrono::duration<float>(now - m_lastImGuiFrameTime).count();
            if (dt >= 0.0005f && dt <= 0.25f) {
                io.DeltaTime = dt;
            } else if (m_lastDeltaTime >= 0.0005f && m_lastDeltaTime <= 0.25f) {
                io.DeltaTime = m_lastDeltaTime;
            } else {
                io.DeltaTime = 1.0f / 120.0f;
            }
        } else {
            io.DeltaTime = (m_lastDeltaTime >= 0.0005f) ? m_lastDeltaTime : (1.0f / 120.0f);
        }
        m_lastImGuiFrameTime = now;
    }
}

void LayerManager::ShutdownImGui() {
    if (m_imguiInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::DestroyContext();
        }
        m_imguiInitialized = false;
    }
}

void LayerManager::RenderImGui(int vpWidth, int vpHeight, const float* surfaceTransformMatrix, bool isSdlHost) {
    EnsureImGui(vpWidth, vpHeight, isSdlHost);

    ImGui_ImplOpenGL3_SetSurfaceTransform(surfaceTransformMatrix);

    if (!isSdlHost) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
    }

    bool dummyDirty = false;
    m_inspectorGui.Render(*this, vpWidth, vpHeight, dummyDirty);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool LayerManager::HandleTouchForImGui(float x, float y, InputPhase phase) {
    EnsureImGui(0, 0, false);
    if (!m_imguiInitialized) return false;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);

    bool isHit = false;
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g != nullptr) {
        for (ImGuiWindow* w : g->Windows) {
            if (!w->WasActive || w->Hidden || (w->Flags & ImGuiWindowFlags_NoInputs))
                continue;
            if (x >= w->Pos.x && x <= (w->Pos.x + w->Size.x) &&
                y >= w->Pos.y && y <= (w->Pos.y + w->Size.y)) {
                isHit = true;
                break;
            }
        }
    }

    if (phase == InputPhase::Down) {
        m_touchCapturedByImGui = isHit;
        io.AddMouseButtonEvent(0, true);
    } else if (phase == InputPhase::Up || phase == InputPhase::Cancel) {
        io.AddMouseButtonEvent(0, false);
        bool wasCaptured = m_touchCapturedByImGui;
        m_touchCapturedByImGui = false;
        return wasCaptured || isHit;
    }

    return m_touchCapturedByImGui || isHit;
}

void LayerManager::RenderLiveInking(int vpWidth, int vpHeight, const float* surfaceTransformMatrix) {
    if (!m_isStrokeActive) return;

    bool isModeler = (m_inspectorGui.currentWorkspaceTab == 0);
    if (isModeler && !m_inspectorGui.showSolidMeshInModeler) return;

    EnsureGl();
    glViewport(0, 0, vpWidth, vpHeight);

    const auto& activePoints = m_liveEngine.GetLiveModeledPoints();
    const auto& predPoints   = m_liveEngine.GetPredictedPoints();

    std::vector<Folio::StrokePoint> combined(activePoints.begin(), activePoints.end());
    if (m_liveEngine.GetConfig().prediction.enable && predPoints.size() > 1) {
        combined.insert(combined.end(), predPoints.begin() + 1, predPoints.end());
    }

    if (combined.empty() && m_lastX > 0.0f) {
        combined.emplace_back(m_lastX, m_lastY, m_activeStyle.baseWidth * 0.5f, m_lastPressure);
    }

    if (combined.empty()) return;

    Folio::Backends::CanvasTransform transform(m_canvasState.panX, m_canvasState.panY, m_canvasState.zoom);

    m_capsuleRenderer.RenderStroke(
        combined,
        Folio::PackPremultipliedGpuColor(m_activeStyle.color),
        transform,
        vpWidth, vpHeight,
        m_activeBrush,
        /*singlePassWet=*/true,
        surfaceTransformMatrix
    );

    glFlush();
}

void LayerManager::RenderDocumentCanvas(int vpWidth, int vpHeight, const float* surfaceTransformMatrix, bool renderStrokes) {
    EnsureGl();
    glViewport(0, 0, vpWidth, vpHeight);

    glClearColor(
        static_cast<float>(m_canvasState.paperColor.r) / 255.0f,
        static_cast<float>(m_canvasState.paperColor.g) / 255.0f,
        static_cast<float>(m_canvasState.paperColor.b) / 255.0f,
        static_cast<float>(m_canvasState.paperColor.a) / 255.0f
    );
    glClear(GL_COLOR_BUFFER_BIT);

    bool isModeler = (m_inspectorGui.currentWorkspaceTab == 0);
    bool shouldRenderStrokes = renderStrokes && (!isModeler || m_inspectorGui.showSolidMeshInModeler);

    if (shouldRenderStrokes) {
        Folio::Backends::CanvasTransform transform(m_canvasState.panX, m_canvasState.panY, m_canvasState.zoom);

        for (const auto& stroke : m_documentStrokes) {
            m_capsuleRenderer.RenderStroke(
                stroke.points,
                stroke.color,
                transform,
                vpWidth, vpHeight,
                stroke.brush,
                /*singlePassWet=*/false,
                surfaceTransformMatrix
            );
        }
    }

    glFlush();
}

void LayerManager::EnsureDocumentFbo(int w, int h) {
    if (w <= 0 || h <= 0) return;
    EnsureGl();

    if (!m_blitProgram) {
#if defined(__ANDROID__)
        const char* header = "#version 300 es\nprecision highp float;\n";
#else
        const char* header = "#version 330 core\n";
#endif
        std::string vsSrc = std::string(header) +
            "layout(location = 0) in vec2 aPos;\n"
            "uniform mat4 uSurfaceTransform;\n"
            "out vec2 vUv;\n"
            "void main() {\n"
            "    vUv = aPos * 0.5 + 0.5;\n"
            "    gl_Position = uSurfaceTransform * vec4(aPos, 0.0, 1.0);\n"
            "}\n";
        std::string fsSrc = std::string(header) +
            "in vec2 vUv;\n"
            "uniform sampler2D uTex;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    fragColor = texture(uTex, vUv);\n"
            "}\n";

        auto compile = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            return s;
        };

        GLuint vs = compile(GL_VERTEX_SHADER, vsSrc.c_str());
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc.c_str());
        m_blitProgram = glCreateProgram();
        glAttachShader(m_blitProgram, vs);
        glAttachShader(m_blitProgram, fs);
        glLinkProgram(m_blitProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_locBlitTex       = glGetUniformLocation(m_blitProgram, "uTex");
        m_locBlitTransform = glGetUniformLocation(m_blitProgram, "uSurfaceTransform");

        static constexpr float kQuad[12] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f,  1.0f
        };
        glGenVertexArrays(1, &m_blitVao);
        glGenBuffers(1, &m_blitVbo);
        glBindVertexArray(m_blitVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_blitVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (m_docFbo && m_docWidth == w && m_docHeight == h) {
        return;
    }

    if (!m_docFbo) glGenFramebuffers(1, &m_docFbo);
    if (!m_docTex) glGenTextures(1, &m_docTex);

    glBindTexture(GL_TEXTURE_2D, m_docTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_docFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_docTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glBindTexture(GL_TEXTURE_2D, 0);

    m_docWidth    = w;
    m_docHeight   = h;
    m_docFboValid = false;
}

void LayerManager::ShutdownDocumentFbo() {
    if (m_docTex)      { glDeleteTextures(1, &m_docTex);         m_docTex = 0; }
    if (m_docFbo)      { glDeleteFramebuffers(1, &m_docFbo);     m_docFbo = 0; }
    if (m_blitVbo)     { glDeleteBuffers(1, &m_blitVbo);         m_blitVbo = 0; }
    if (m_blitVao)     { glDeleteVertexArrays(1, &m_blitVao);    m_blitVao = 0; }
    if (m_blitProgram) { glDeleteProgram(m_blitProgram);         m_blitProgram = 0; }
    m_docWidth = m_docHeight = 0;
    m_docFboValid = false;
}

void LayerManager::BlitDocumentFboToFrontBuffer(const float* surfaceTransformMatrix) {
    if (!m_docFbo || !m_docTex || !m_blitProgram) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND); // Opaque overwrite — zero glClear needed on front buffer!

    glUseProgram(m_blitProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_docTex);
    glUniform1i(m_locBlitTex, 0);

    if (m_locBlitTransform >= 0) {
        if (surfaceTransformMatrix != nullptr) {
            glUniformMatrix4fv(m_locBlitTransform, 1, GL_FALSE, surfaceTransformMatrix);
        } else {
            static const float kIdentity[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            glUniformMatrix4fv(m_locBlitTransform, 1, GL_FALSE, kIdentity);
        }
    }

    glBindVertexArray(m_blitVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void LayerManager::ApplyTransformedScissor(const ScreenRect& logicalRect, int vpWidth, int vpHeight, const float* surfaceTransformMatrix) {
    if (!logicalRect.valid || logicalRect.w <= 0 || logicalRect.h <= 0 || vpWidth <= 0 || vpHeight <= 0) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    // Query physical bound viewport dimensions (in case hardware buffer rotated width/height)
    GLint physVp[4] = { 0, 0, vpWidth, vpHeight };
    glGetIntegerv(GL_VIEWPORT, physVp);
    const int physW = (physVp[2] > 0) ? physVp[2] : vpWidth;
    const int physH = (physVp[3] > 0) ? physVp[3] : vpHeight;

    Folio::Backends::LogicalRect lRect{ logicalRect.x, logicalRect.y, logicalRect.w, logicalRect.h, logicalRect.valid };
    const auto scissor = Folio::Backends::CalculatePhysicalScissor(
        lRect, vpWidth, vpHeight, surfaceTransformMatrix, physW, physH
    );

    if (scissor.enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor.x, scissor.y, scissor.w, scissor.h);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void LayerManager::RenderStateADocumentEngine(int vpWidth, int vpHeight, const float* surfaceTransformMatrix, bool isSdlHost) {
    if (vpWidth <= 0 || vpHeight <= 0) return;

    const auto startPass = std::chrono::steady_clock::now();
    if (m_lastRenderTimePoint.time_since_epoch().count() > 0) {
        const float dt = std::chrono::duration<float>(startPass - m_lastRenderTimePoint).count();
        if (dt >= 0.0005f && dt <= 0.25f) {
            const float instantFps = 1.0f / dt;
            m_telemetry.displayFps = (m_telemetry.displayFps <= 0.0f)
                ? instantFps
                : (m_telemetry.displayFps * 0.90f + instantFps * 0.10f);
            m_telemetry.frameLatencyMs = 1000.0f / m_telemetry.displayFps;
            m_lastDeltaTime = dt;
        }
    }
    m_lastRenderTimePoint = startPass;

    EnsureDocumentFbo(vpWidth, vpHeight);

    GLint prevFbo = 0;
    GLint physVp[4] = { 0, 0, vpWidth, vpHeight };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, physVp);

    glDisable(GL_SCISSOR_TEST);

    // 1. Render complete Document Canvas + Committed Strokes + Dear ImGui into offscreen m_docFbo (unrotated)
    glBindFramebuffer(GL_FRAMEBUFFER, m_docFbo);
    glViewport(0, 0, vpWidth, vpHeight);

    RenderDocumentCanvas(vpWidth, vpHeight, /*surfaceTransformMatrix=*/nullptr, /*renderStrokes=*/true);
    RenderImGui(vpWidth, vpHeight, /*surfaceTransformMatrix=*/nullptr, isSdlHost);

    m_docFboValid = true;
    m_wetUploadedModeledCount = 0;
    m_prevPredictionDirtyRect.valid = false;
    m_lastTelemetryHudUpdateSec = m_telemetry.timestampSec;

    // 2. Restore hardware front buffer (prevFbo) and present via single opaque textured quad pass (ZERO glClear!)
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(physVp[0], physVp[1], physVp[2], physVp[3]);
    glDisable(GL_SCISSOR_TEST);

    BlitDocumentFboToFrontBuffer(surfaceTransformMatrix);
    glFlush();

    const auto endPass = std::chrono::steady_clock::now();
    m_telemetry.passDurationMs = std::chrono::duration<float, std::milli>(endPass - startPass).count();
}

void LayerManager::RenderStateBWetScanout(int vpWidth, int vpHeight, const float* surfaceTransformMatrix) {
    if (!m_isStrokeActive || vpWidth <= 0 || vpHeight <= 0) return;

    const auto startPass = std::chrono::steady_clock::now();
    if (m_lastRenderTimePoint.time_since_epoch().count() > 0) {
        const float dt = std::chrono::duration<float>(startPass - m_lastRenderTimePoint).count();
        if (dt >= 0.0005f && dt <= 0.25f) {
            const float instantFps = 1.0f / dt;
            m_telemetry.displayFps = (m_telemetry.displayFps <= 0.0f)
                ? instantFps
                : (m_telemetry.displayFps * 0.90f + instantFps * 0.10f);
            m_telemetry.frameLatencyMs = 1000.0f / m_telemetry.displayFps;
            m_lastDeltaTime = dt;
        }
    }
    m_lastRenderTimePoint = startPass;

    // If the offscreen Document FBO hasn't been baked yet (e.g. first frame of testbed), bake it once
    if (!m_docFboValid || m_docWidth != vpWidth || m_docHeight != vpHeight) {
        RenderStateADocumentEngine(vpWidth, vpHeight, surfaceTransformMatrix, /*isSdlHost=*/false);
    }

    EnsureGl();
    glViewport(0, 0, vpWidth, vpHeight);

    const auto& activePoints = m_liveEngine.GetLiveModeledPoints();
    const auto& predPoints   = m_liveEngine.GetPredictedPoints();
    const bool usePrediction = m_liveEngine.GetConfig().prediction.enable && (predPoints.size() > 1);

    // Build combined point stream (confirmed modeled + temporary Kalman predicted tail)
    std::vector<Folio::StrokePoint> combined(activePoints.begin(), activePoints.end());
    if (usePrediction) {
        combined.insert(combined.end(), predPoints.begin() + 1, predPoints.end());
    }

    if (combined.empty() && m_lastX > 0.0f) {
        const float wx = (m_lastX - m_canvasState.panX) / m_canvasState.zoom;
        const float wy = (m_lastY - m_canvasState.panY) / m_canvasState.zoom;
        combined.emplace_back(wx, wy, m_activeStyle.baseWidth * 0.5f, m_lastPressure);
    }

    if (combined.empty()) return;

    // Compute tight screen-space AABB around the newly added modeled segment + active prediction tail
    const size_t startIdx = (m_wetUploadedModeledCount > 2) ? (m_wetUploadedModeledCount - 2) : 0;
    float minSx =  1e9f, minSy =  1e9f;
    float maxSx = -1e9f, maxSy = -1e9f;

    for (size_t i = startIdx; i < combined.size(); ++i) {
        const float sx = static_cast<float>(combined[i].x) * m_canvasState.zoom + m_canvasState.panX;
        const float sy = static_cast<float>(combined[i].y) * m_canvasState.zoom + m_canvasState.panY;
        const float rPx = std::max(combined[i].r * m_canvasState.zoom, m_activeStyle.baseWidth * m_canvasState.zoom) + 12.0f;
        minSx = std::min(minSx, sx - rPx);
        minSy = std::min(minSy, sy - rPx);
        maxSx = std::max(maxSx, sx + rPx);
        maxSy = std::max(maxSy, sy + rPx);
    }

    // Also include the latest raw input sample in the dirty rect so red crosshairs/raw polyline are never clipped
    if (!m_lastRawSamples.empty()) {
        const auto& lastRaw = m_lastRawSamples.back();
        const float rsx = static_cast<float>(lastRaw.x) * m_canvasState.zoom + m_canvasState.panX;
        const float rsy = static_cast<float>(lastRaw.y) * m_canvasState.zoom + m_canvasState.panY;
        minSx = std::min(minSx, rsx - 16.0f);
        minSy = std::min(minSy, rsy - 16.0f);
        maxSx = std::max(maxSx, rsx + 16.0f);
        maxSy = std::max(maxSy, rsy + 16.0f);
    }

    ScreenRect currentTipRect{};
    if (maxSx > minSx && maxSy > minSy) {
        currentTipRect.x = std::clamp(static_cast<int>(std::floor(minSx)), 0, vpWidth);
        currentTipRect.y = std::clamp(static_cast<int>(std::floor(minSy)), 0, vpHeight);
        int x1 = std::clamp(static_cast<int>(std::ceil(maxSx)), 0, vpWidth);
        int y1 = std::clamp(static_cast<int>(std::ceil(maxSy)), 0, vpHeight);
        currentTipRect.w = std::max(0, x1 - currentTipRect.x);
        currentTipRect.h = std::max(0, y1 - currentTipRect.y);
        currentTipRect.valid = (currentTipRect.w > 0 && currentTipRect.h > 0);
    }

    // Union previous frame's prediction tail AABB with current tip AABB
    ScreenRect unionRect = currentTipRect;
    if (m_prevPredictionDirtyRect.valid && unionRect.valid) {
        int ux0 = std::min(unionRect.x, m_prevPredictionDirtyRect.x);
        int uy0 = std::min(unionRect.y, m_prevPredictionDirtyRect.y);
        int ux1 = std::max(unionRect.x + unionRect.w, m_prevPredictionDirtyRect.x + m_prevPredictionDirtyRect.w);
        int uy1 = std::max(unionRect.y + unionRect.h, m_prevPredictionDirtyRect.y + m_prevPredictionDirtyRect.h);
        unionRect.x = ux0;
        unionRect.y = uy0;
        unionRect.w = ux1 - ux0;
        unionRect.h = uy1 - uy0;
        unionRect.valid = (unionRect.w > 0 && unionRect.h > 0);
    } else if (m_prevPredictionDirtyRect.valid) {
        unionRect = m_prevPredictionDirtyRect;
    }

    m_prevPredictionDirtyRect = currentTipRect;
    if (activePoints.size() > 1) {
        m_wetUploadedModeledCount = activePoints.size() - 1;
    }

    // 1. Apply tight hardware scissor around the dirty pen tip & prediction region
    if (unionRect.valid) {
        ApplyTransformedScissor(unionRect, vpWidth, vpHeight, surfaceTransformMatrix);
        // Restore background + UI patch from m_docFbo inside the scissor box (erasing old prediction trail with zero glClear!)
        BlitDocumentFboToFrontBuffer(surfaceTransformMatrix);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    // 2. Render wet stroke capsules inside the scissored dirty box (if enabled for current tab)
    bool isModeler = (m_inspectorGui.currentWorkspaceTab == 0);
    if (!isModeler || m_inspectorGui.showSolidMeshInModeler) {
        Folio::Backends::CanvasTransform transform(m_canvasState.panX, m_canvasState.panY, m_canvasState.zoom);
        m_capsuleRenderer.RenderStroke(
            combined,
            Folio::PackPremultipliedGpuColor(m_activeStyle.color),
            transform,
            vpWidth, vpHeight,
            m_activeBrush,
            /*singlePassWet=*/true,
            surfaceTransformMatrix
        );
    }

    // 3. In diagnostic tabs (Workspace 0 & 1), render live splines, crosshairs & prediction trail inside the scissored box!
    if ((m_inspectorGui.currentWorkspaceTab == 0 || m_inspectorGui.currentWorkspaceTab == 1) && unionRect.valid) {
        EnsureImGui(vpWidth, vpHeight, false);
        ImGui_ImplOpenGL3_SetSurfaceTransform(surfaceTransformMatrix);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        RenderDiagnosticsOverlay(m_inspectorGui, *this, vpWidth, vpHeight, /*drawBackgroundGrid=*/false);
        ImGui::Render();
        ApplyTransformedScissor(unionRect, vpWidth, vpHeight, surfaceTransformMatrix);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // 4. Throttled (~15 Hz, every ~66ms) Partial Scissored Telemetry HUD Update
    if ((m_telemetry.timestampSec - m_lastTelemetryHudUpdateSec) >= 0.066 || m_lastTelemetryHudUpdateSec <= 0.0) {
        m_lastTelemetryHudUpdateSec = m_telemetry.timestampSec;

        EnsureImGui(vpWidth, vpHeight, false);
        ImGui_ImplOpenGL3_SetSurfaceTransform(surfaceTransformMatrix);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ScreenRect hudRect{};
        m_inspectorGui.RenderTelemetryHud(*this, /*drawOpaqueBackdrop=*/true,
                                          &hudRect.x, &hudRect.y, &hudRect.w, &hudRect.h);
        ImGui::Render();

        hudRect.valid = (hudRect.w > 0 && hudRect.h > 0);
        if (hudRect.valid) {
            ApplyTransformedScissor(hudRect, vpWidth, vpHeight, surfaceTransformMatrix);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glFlush();

    const auto endPass = std::chrono::steady_clock::now();
    m_telemetry.passDurationMs = std::chrono::duration<float, std::milli>(endPass - startPass).count();
}

void LayerManager::ReplayRawStroke() {
    if (m_lastRawSamples.empty()) return;

    m_liveEngine.BeginStroke(
        m_lastRawSamples[0].x, m_lastRawSamples[0].y,
        m_lastRawSamples[0].pressure, m_lastRawSamples[0].timestampSec,
        m_activeStyle, m_canvasState.zoom,
        m_lastRawSamples[0].tiltX, m_lastRawSamples[0].tiltY
    );
    for (size_t i = 1; i < m_lastRawSamples.size(); ++i) {
        m_liveEngine.AppendPoint(
            m_lastRawSamples[i].x, m_lastRawSamples[i].y,
            m_lastRawSamples[i].pressure, m_lastRawSamples[i].timestampSec,
            m_lastRawSamples[i].tiltX, m_lastRawSamples[i].tiltY
        );
    }
    Folio::FinishedStrokeData fin = m_liveEngine.FinishStroke();
    m_lastRawModeledPoints = std::move(fin.rawModeledPoints);
    m_lastFinishedPoints   = std::move(fin.modeledPoints);
    if (!m_documentStrokes.empty() && !m_lastFinishedPoints.empty()) {
        m_documentStrokes.back().points = m_lastFinishedPoints;
        m_documentStrokes.back().brush = m_activeBrush;
        m_documentStrokes.back().color = Folio::PackPremultipliedGpuColor(m_activeStyle.color);
    } else if (!m_lastFinishedPoints.empty()) {
        DocumentStroke stroke;
        stroke.points = m_lastFinishedPoints;
        stroke.brush = m_activeBrush;
        stroke.color = Folio::PackPremultipliedGpuColor(m_activeStyle.color);
        m_documentStrokes.push_back(std::move(stroke));
    }
    if (onDocumentDirty) {
        onDocumentDirty();
    }
}

void LayerManager::SetLastFinishedPoints(std::vector<Folio::StrokePoint> points) {
    m_lastFinishedPoints = std::move(points);
    if (!m_documentStrokes.empty() && !m_lastFinishedPoints.empty()) {
        m_documentStrokes.back().points = m_lastFinishedPoints;
        m_documentStrokes.back().brush = m_activeBrush;
        m_documentStrokes.back().color = Folio::PackPremultipliedGpuColor(m_activeStyle.color);
    } else if (!m_lastFinishedPoints.empty()) {
        DocumentStroke stroke;
        stroke.points = m_lastFinishedPoints;
        stroke.brush = m_activeBrush;
        stroke.color = Folio::PackPremultipliedGpuColor(m_activeStyle.color);
        m_documentStrokes.push_back(std::move(stroke));
    }
    if (onDocumentDirty) {
        onDocumentDirty();
    }
}

void LayerManager::InjectTestScribble() {
    Folio::V2::InjectTestScribble(*this);
}

void LayerManager::LoadRepro114() {
    Folio::V2::LoadRepro114(*this);
}

void LayerManager::AddDocumentStroke(DocumentStroke stroke) {
    if (m_isTestbedMode) {
        m_documentStrokes.clear();
    }
    m_documentStrokes.push_back(std::move(stroke));
    m_docFboValid = false;
    if (onDocumentDirty) {
        onDocumentDirty();
    }
}

void LayerManager::ClearDocument() {
    m_liveEngine.CancelStroke();
    m_documentStrokes.clear();
    m_isStrokeActive = false;
    m_docFboValid = false;

    if (onDocumentDirty) {
        onDocumentDirty();
    }
}

} // namespace Folio::V2
