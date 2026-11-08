/**
 * =========================================================================================
 * @file folio_backends/android/folio_android_jni.cpp
 * @brief Android JNI Bridge for Sub-4ms Zero-Latency Hardware Scanout Inking
 * =========================================================================================
 */

#include <jni.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>

#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"
#include "inspectorV2.0/app_brain.hpp"
#include "inspectorV2.0/ui_manager.hpp"

#define LOG_TAG "FolioSketchNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_SWAP(...) __android_log_print(ANDROID_LOG_VERBOSE, "FolioSwap", __VA_ARGS__)
#define LOG_SWAP_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "FolioSwap", __VA_ARGS__)

namespace {

struct CommittedNativeStroke {
    std::vector<Folio::StrokePoint> points;
    uint32_t color = 0;
    Folio::Backends::CapsuleBrush brush{};
};

struct FolioAndroidSession {
    Folio::InkEngine                    engine;
    Folio::Backends::GlCapsuleRenderer  capsuleRenderer;
    Folio::StrokeStyle                  activeStyle{};
    Folio::Backends::CapsuleBrush       activeBrush{};
    Folio::Backends::CanvasTransform    transform{};

    std::vector<CommittedNativeStroke>  committedStrokes;

    float lastX = 0.0f;
    float lastY = 0.0f;
    float lastPressure = 0.5f;

    bool isStrokeActive = false;
    bool glInitialized  = false;
    uint64_t wetFlushCount = 0;

    FolioAndroidSession() {
        activeStyle.baseWidth = 12.0f;
        activeStyle.color     = Folio::ColorRgba(0, 100, 255, 255); // Electric Blue (bold, thick, 100% visible!)
        activeBrush.opacity   = 1.0f;
    }

    void EnsureGl() {
        if (!glInitialized) {
            capsuleRenderer.Init();
            Folio::V2::UiManager::Instance().Init();
            glInitialized = true;
            LOGI("FolioAndroidSession GL resources initialized");
        }
    }
};

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeCreateEngine(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto* session = new FolioAndroidSession();
    LOGI("Created native Folio session: %p", session);
    return reinterpret_cast<jlong>(session);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeDestroyEngine(JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (session) {
        session->capsuleRenderer.Shutdown();
        delete session;
        LOGI("Destroyed native Folio session");
    }
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeIngestPacket(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jlong handle,
    jfloat x, jfloat y, jfloat pressure,
    jfloat tiltX, jfloat tiltY,
    jlong timestampNs,
    jboolean isDown, jboolean isUp, jboolean isStylus,
    jint pointerId)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    const double timeSec = static_cast<double>(timestampNs) * 1e-9;

    // Check if Dear ImGui wants to capture this interaction
    bool uiCaptured = Folio::V2::UiManager::Instance().ProcessTouch(x, y, isDown, isUp);
    if (uiCaptured && isDown) {
        // ImGui widget clicked: request multi-buffered redraw to update UI state
        Folio::V2::AppBrain::Instance().RequestRedraw();
        return;
    }

    // Route packet through AppBrain's InputManager for Palm Rejection & Two-Finger Navigation
    Folio::V2::PointerPacket pkt;
    pkt.device       = isStylus ? Folio::V2::DeviceType::Stylus : Folio::V2::DeviceType::Touch;
    pkt.phase        = isDown ? Folio::V2::InputPhase::Down : (isUp ? Folio::V2::InputPhase::Up : Folio::V2::InputPhase::Move);
    pkt.pointerId    = static_cast<int32_t>(pointerId);
    pkt.x            = x;
    pkt.y            = y;
    pkt.pressure     = pressure;
    pkt.tiltX        = tiltX;
    pkt.tiltY        = tiltY;
    pkt.timestampSec = timeSec;
    Folio::V2::AppBrain::Instance().GetLayerManager().ProcessPointer(pkt);

    if (!isStylus) {
        // Finger touch handled exclusively by InputManager (Pan & Zoom); does NOT draw ink
        return;
    }

    session->lastX = x;
    session->lastY = y;
    session->lastPressure = pressure;

    if (isDown) {
        session->isStrokeActive = true;
        session->engine.BeginStroke(x, y, pressure, timeSec,
                                    session->activeStyle, session->transform.zoom,
                                    tiltX, tiltY);
    } else if (session->isStrokeActive) {
        session->engine.AppendPoint(x, y, pressure, timeSec, tiltX, tiltY);
    }

    if (isUp && session->isStrokeActive) {
        Folio::FinishedStrokeData fin = session->engine.FinishStroke();
        if (!fin.modeledPoints.empty()) {
            CommittedNativeStroke stroke;
            stroke.points = fin.modeledPoints;
            stroke.color  = Folio::PackPremultipliedGpuColor(session->activeStyle.color);
            stroke.brush  = session->activeBrush;

            // Commit to both local session and central AppBrain LayerManager
            Folio::V2::DocumentStroke docStroke;
            docStroke.points = std::move(fin.modeledPoints);
            docStroke.color  = stroke.color;
            docStroke.brush  = stroke.brush;
            Folio::V2::AppBrain::Instance().GetLayerManager().AddDocumentStroke(std::move(docStroke));

            session->committedStrokes.push_back(std::move(stroke));
        } else if (session->lastX > 0.0f) {
            CommittedNativeStroke stroke;
            stroke.points.emplace_back(session->lastX, session->lastY, session->activeStyle.baseWidth * 0.5f, session->lastPressure);
            stroke.color  = Folio::PackPremultipliedGpuColor(session->activeStyle.color);
            stroke.brush  = session->activeBrush;

            Folio::V2::DocumentStroke docStroke;
            docStroke.points = stroke.points;
            docStroke.color  = stroke.color;
            docStroke.brush  = stroke.brush;
            Folio::V2::AppBrain::Instance().GetLayerManager().AddDocumentStroke(std::move(docStroke));

            session->committedStrokes.push_back(std::move(stroke));
        }
        session->isStrokeActive = false;

        // Notify the C++ SDL host brain that a stroke was committed
        Folio::V2::AppBrain::Instance().RequestRedraw();
    }
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderWet(
    JNIEnv* env, jclass /*clazz*/,
    jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session || !session->isStrokeActive) return;

    session->EnsureGl();
    glViewport(0, 0, width, height);

    const auto& activePoints = session->engine.GetLiveModeledPoints();
    const auto& predPoints   = session->engine.GetPredictedPoints();

    std::vector<Folio::StrokePoint> combined(activePoints.begin(), activePoints.end());
    if (session->engine.GetConfig().prediction.enable && predPoints.size() > 1) {
        combined.insert(combined.end(), predPoints.begin() + 1, predPoints.end());
    }

    if (combined.empty() && session->lastX > 0.0f) {
        combined.emplace_back(session->lastX, session->lastY, session->activeStyle.baseWidth * 0.5f, session->lastPressure);
    }

    if (combined.empty()) return;

    jfloat* matrixElements = nullptr;
    if (surfaceTransform != nullptr && env->GetArrayLength(surfaceTransform) >= 16) {
        matrixElements = env->GetFloatArrayElements(surfaceTransform, nullptr);
    }

    session->capsuleRenderer.RenderStroke(
        combined,
        Folio::PackPremultipliedGpuColor(session->activeStyle.color),
        session->transform,
        width, height,
        session->activeBrush,
        /*singlePassWet=*/true,
        matrixElements
    );

    if (matrixElements != nullptr) {
        env->ReleaseFloatArrayElements(surfaceTransform, matrixElements, JNI_ABORT);
    }

    glFlush();
    session->wetFlushCount++;
    LOG_SWAP("glFlush executed during active live inking (count: %llu)", (unsigned long long)session->wetFlushCount);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderDry(
    JNIEnv* env, jclass /*clazz*/,
    jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    LOG_SWAP("Stroke completed (ACTION_UP) - presenting dry document frame (total inking flushes: %llu)", (unsigned long long)session->wetFlushCount);
    session->wetFlushCount = 0;

    session->EnsureGl();
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // True Black AMOLED canvas background
    glClear(GL_COLOR_BUFFER_BIT);

    jfloat* matrixElements = nullptr;
    if (surfaceTransform != nullptr && env->GetArrayLength(surfaceTransform) >= 16) {
        matrixElements = env->GetFloatArrayElements(surfaceTransform, nullptr);
    }

    for (const auto& stroke : session->committedStrokes) {
        session->capsuleRenderer.RenderStroke(
            stroke.points,
            stroke.color,
            session->transform,
            width, height,
            stroke.brush,
            /*singlePassWet=*/false,
            matrixElements
        );
    }

    if (matrixElements != nullptr) {
        env->ReleaseFloatArrayElements(surfaceTransform, matrixElements, JNI_ABORT);
    }

    // Render Dear ImGui Diagnostic Bench on the MultiBuffered sublayer
    Folio::V2::UiManager::Instance().Render(width, height);

    glFlush();
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeClearCanvas(
    JNIEnv* /*env*/, jclass /*clazz*/, jlong handle)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    session->engine.CancelStroke();
    session->committedStrokes.clear();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
}

} // extern "C"
