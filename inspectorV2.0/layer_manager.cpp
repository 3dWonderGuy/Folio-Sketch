/**
 * =========================================================================================
 * @file inspectorV2.0/layer_manager.cpp
 * @brief Implementation of Unified Dual-Layer Manager (Live Inking + Standard Canvas)
 * =========================================================================================
 */

#include "inspectorV2.0/layer_manager.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

namespace Folio::V2 {

LayerManager::LayerManager() {
    m_activeStyle.baseWidth = 10.0f;
    m_activeStyle.color     = Folio::ColorRgba(0, 100, 255, 255); // Electric Blue
    m_activeBrush.opacity   = 1.0f;

    InitInputRouting();
}

LayerManager::~LayerManager() {
    if (m_glInitialized) {
        m_capsuleRenderer.Shutdown();
        m_glInitialized = false;
    }
}

void LayerManager::InitInputRouting() {
    // 1. Live Inking Fast Path (Bypass System)
    m_inputManager.onInkBegin = [this](const PointerPacket& pkt) {
        m_isStrokeActive = true;
        m_lastX = pkt.x;
        m_lastY = pkt.y;
        m_lastPressure = pkt.pressure;

        // Unproject screen coordinates into document world coordinates
        const float worldX = (pkt.x - m_canvasState.panX) / m_canvasState.zoom;
        const float worldY = (pkt.y - m_canvasState.panY) / m_canvasState.zoom;

        m_liveEngine.BeginStroke(
            worldX, worldY, pkt.pressure, pkt.timestampSec,
            m_activeStyle, m_canvasState.zoom,
            pkt.tiltX, pkt.tiltY
        );
    };

    m_inputManager.onInkMove = [this](const PointerPacket& pkt) {
        m_lastX = pkt.x;
        m_lastY = pkt.y;
        m_lastPressure = pkt.pressure;

        // Unproject screen coordinates into document world coordinates
        const float worldX = (pkt.x - m_canvasState.panX) / m_canvasState.zoom;
        const float worldY = (pkt.y - m_canvasState.panY) / m_canvasState.zoom;

        m_liveEngine.AppendPoint(
            worldX, worldY, pkt.pressure, pkt.timestampSec,
            pkt.tiltX, pkt.tiltY
        );
    };

    m_inputManager.onInkEnd = [this](const PointerPacket& pkt) {
        m_lastX = pkt.x;
        m_lastY = pkt.y;
        m_lastPressure = pkt.pressure;

        if (m_isStrokeActive) {
            Folio::FinishedStrokeData fin = m_liveEngine.FinishStroke();
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

            // Signal standard canvas that a new stroke was committed
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

        // Keep focal point stationary in screen coordinates during zoom:
        // (focalX - panX_new) / newZoom == (focalX - panX_old) / oldZoom
        m_canvasState.panX = focalX - (focalX - m_canvasState.panX) * (newZoom / oldZoom);
        m_canvasState.panY = focalY - (focalY - m_canvasState.panY) * (newZoom / oldZoom);
        m_canvasState.zoom = newZoom;

        if (onDocumentDirty) {
            onDocumentDirty();
        }
    };
}

void LayerManager::ProcessPointer(const PointerPacket& packet) {
    m_inputManager.ProcessPacket(packet);
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

void LayerManager::RenderLiveInking(int vpWidth, int vpHeight, const float* surfaceTransformMatrix) {
    if (!m_isStrokeActive) return;

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

void LayerManager::RenderDocumentCanvas(int vpWidth, int vpHeight, const float* surfaceTransformMatrix) {
    EnsureGl();
    glViewport(0, 0, vpWidth, vpHeight);

    // Paper background
    glClearColor(
        static_cast<float>(m_canvasState.paperColor.r) / 255.0f,
        static_cast<float>(m_canvasState.paperColor.g) / 255.0f,
        static_cast<float>(m_canvasState.paperColor.b) / 255.0f,
        static_cast<float>(m_canvasState.paperColor.a) / 255.0f
    );
    glClear(GL_COLOR_BUFFER_BIT);

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

    glFlush();
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
