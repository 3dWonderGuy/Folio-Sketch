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

#define LOG_TAG "FolioSketchNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

    FolioAndroidSession() {
        activeStyle.baseWidth = 12.0f;
        activeStyle.color     = Folio::ColorRgba(0, 100, 255, 255); // Electric Blue (bold, thick, 100% visible!)
        activeBrush.opacity   = 1.0f;
    }

    void EnsureGl() {
        if (!glInitialized) {
            capsuleRenderer.Init();
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
    jboolean isDown, jboolean isUp, jboolean /*isStylus*/)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    session->lastX = x;
    session->lastY = y;
    session->lastPressure = pressure;

    const double timeSec = static_cast<double>(timestampNs) * 1e-9;

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
            stroke.points = std::move(fin.modeledPoints);
            stroke.color  = Folio::PackPremultipliedGpuColor(session->activeStyle.color);
            stroke.brush  = session->activeBrush;
            session->committedStrokes.push_back(std::move(stroke));
        } else if (session->lastX > 0.0f) {
            CommittedNativeStroke stroke;
            stroke.points.emplace_back(session->lastX, session->lastY, session->activeStyle.baseWidth * 0.5f, session->lastPressure);
            stroke.color  = Folio::PackPremultipliedGpuColor(session->activeStyle.color);
            stroke.brush  = session->activeBrush;
            session->committedStrokes.push_back(std::move(stroke));
        }
        session->isStrokeActive = false;
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
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderDry(
    JNIEnv* env, jclass /*clazz*/,
    jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    session->EnsureGl();
    glViewport(0, 0, width, height);
    glClearColor(0.96f, 0.96f, 0.98f, 1.0f); // Light off-white paper canvas background
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
