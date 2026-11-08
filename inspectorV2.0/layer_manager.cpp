/**
 * =========================================================================================
 * @file inspectorV2.0/layer_manager.cpp
 * @brief Implementation of Unified Dual-Layer Manager (Live Inking + Standard Canvas)
 * =========================================================================================
 */

#include "inspectorV2.0/layer_manager.hpp"
#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/testbed/testbed_samples.hpp"
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
        if (m_isTestbedMode) {
            m_documentStrokes.clear();
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
    io.DeltaTime = 1.0f / 60.0f;
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
    if (onDocumentDirty) {
        onDocumentDirty();
    }
}

void LayerManager::ClearDocument() {
    m_liveEngine.CancelStroke();
    m_documentStrokes.clear();
    m_isStrokeActive = false;

    if (onDocumentDirty) {
        onDocumentDirty();
    }
}

} // namespace Folio::V2
