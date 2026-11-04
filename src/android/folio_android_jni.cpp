/**
 * =========================================================================================
 * @file src/android/folio_android_jni.cpp
 * @brief Android JNI Bridge for Zero-Latency Inking & Wet/Dry Compositor
 * =========================================================================================
 */

#include <jni.h>
#include <android/log.h>
#include <memory>
#include <vector>

#include "folio_sketch/engine/ink_engine.hpp"
#include "debug/gl_capsule_renderer.hpp"
#include "debug/wet_dry_compositor.hpp"
#include "debug/gl_loader.hpp"

#define LOG_TAG "FolioSketchNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

struct FolioAndroidSession {
    Folio::InkEngine               engine;
    Folio::GlCapsuleRenderer       capsuleRenderer;
    Folio::Debug::WetDryCompositor compositor;
    Folio::StrokeStyle             activeStyle{};
    Folio::CapsuleBrush            activeBrush{};
    Folio::Debug::CanvasTransform  transform{};

    bool isStrokeActive = false;
    bool glInitialized  = false;

    FolioAndroidSession() {
        activeStyle.baseWidth = 2.4f;
        activeStyle.color     = Folio::ColorRgba(24, 28, 36, 255); // Near black
        activeBrush.opacity   = 1.0f;
    }

    void EnsureGl() {
        if (!glInitialized) {
            capsuleRenderer.Init();
            compositor.Init();
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
        session->compositor.Shutdown();
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
        session->isStrokeActive = false;

        // Pillar 2 Commit: Render finished stroke directly into the persistent Dry FBO
        if (session->glInitialized && !fin.modeledPoints.empty()) {
            session->compositor.CommitStroke(
                session->capsuleRenderer,
                fin.modeledPoints,
                Folio::PackPremultipliedGpuColor(session->activeStyle.color),
                session->transform,
                session->activeBrush
            );
        }
    }
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeIngestBatch(
    JNIEnv* env, jclass /*clazz*/,
    jlong handle,
    jfloatArray xCoords,
    jfloatArray yCoords,
    jfloatArray pressures,
    jlongArray timestampsNs,
    jint count,
    jboolean isDown,
    jboolean isUp)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session || count <= 0) return;

    jfloat* x = env->GetFloatArrayElements(xCoords, nullptr);
    jfloat* y = env->GetFloatArrayElements(yCoords, nullptr);
    jfloat* p = env->GetFloatArrayElements(pressures, nullptr);
    jlong*  t = env->GetLongArrayElements(timestampsNs, nullptr);

    for (int i = 0; i < count; ++i) {
        const double timeSec = static_cast<double>(t[i]) * 1e-9;
        const bool ptDown = isDown && (i == 0);
        const bool ptUp   = isUp && (i == count - 1);

        if (ptDown) {
            session->isStrokeActive = true;
            session->engine.BeginStroke(x[i], y[i], p[i], timeSec,
                                        session->activeStyle, session->transform.zoom,
                                        0.0f, 0.0f);
        } else if (session->isStrokeActive) {
            session->engine.AppendPoint(x[i], y[i], p[i], timeSec, 0.0f, 0.0f);
        }

        if (ptUp && session->isStrokeActive) {
            Folio::FinishedStrokeData fin = session->engine.FinishStroke();
            session->isStrokeActive = false;
            if (session->glInitialized && !fin.modeledPoints.empty()) {
                session->compositor.CommitStroke(
                    session->capsuleRenderer,
                    fin.modeledPoints,
                    Folio::PackPremultipliedGpuColor(session->activeStyle.color),
                    session->transform,
                    session->activeBrush
                );
            }
        }
    }

    env->ReleaseFloatArrayElements(xCoords, x, JNI_ABORT);
    env->ReleaseFloatArrayElements(yCoords, y, JNI_ABORT);
    env->ReleaseFloatArrayElements(pressures, p, JNI_ABORT);
    env->ReleaseLongArrayElements(timestampsNs, t, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderWet(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jlong handle, jint width, jint height)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session || !session->isStrokeActive) return;

    session->EnsureGl();

    // Direct GPU Capsule SDF drawing of in-flight modeled points + Kalman forward predictions
    auto activePoints = session->engine.GetLiveModeledPoints();
    if (activePoints.empty()) return;

    auto predPoints = session->engine.GetPredictedPoints();

    session->compositor.RenderWet(
        session->capsuleRenderer,
        activePoints,
        predPoints,
        Folio::PackPremultipliedGpuColor(session->activeStyle.color),
        session->transform,
        width, height,
        session->activeBrush
    );
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderDry(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jlong handle, jint width, jint height)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    session->EnsureGl();
    session->compositor.EnsureSize(width, height);
    session->compositor.PresentDry(width, height);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeClearCanvas(
    JNIEnv* /*env*/, jclass /*clazz*/, jlong handle)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    session->engine.CancelStroke();
    session->compositor.ClearDry(0.0f, 0.0f, 0.0f, 0.0f);
}

} // extern "C"
