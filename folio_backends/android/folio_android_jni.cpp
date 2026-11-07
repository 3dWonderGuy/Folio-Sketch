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

#include "inspectorV2.0/layer_manager.hpp"

#define LOG_TAG "FolioSketchNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

struct FolioAndroidSession {
    Folio::V2::LayerManager layerManager;
};

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeCreateEngine(JNIEnv* /*env*/, jclass /*clazz*/) {
    auto* session = new FolioAndroidSession();
    LOGI("Created native Folio session with LayerManager: %p", session);
    return reinterpret_cast<jlong>(session);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeDestroyEngine(JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (session) {
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

    Folio::V2::PointerPacket pkt;
    pkt.device = isStylus ? Folio::V2::DeviceType::Stylus : Folio::V2::DeviceType::Touch;
    pkt.pointerId = pointerId;
    if (isDown) {
        pkt.phase = Folio::V2::InputPhase::Down;
    } else if (isUp) {
        pkt.phase = Folio::V2::InputPhase::Up;
    } else {
        pkt.phase = Folio::V2::InputPhase::Move;
    }

    pkt.x = x;
    pkt.y = y;
    pkt.pressure = pressure;
    pkt.tiltX = tiltX;
    pkt.tiltY = tiltY;
    pkt.timestampSec = static_cast<double>(timestampNs) * 1e-9;

    session->layerManager.ProcessPointer(pkt);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderWet(
    JNIEnv* env, jclass /*clazz*/,
    jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    jfloat* matrixElements = nullptr;
    if (surfaceTransform != nullptr && env->GetArrayLength(surfaceTransform) >= 16) {
        matrixElements = env->GetFloatArrayElements(surfaceTransform, nullptr);
    }

    if (matrixElements != nullptr) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            LOGI("SurfaceTransform matrix: [%.2f, %.2f, %.2f, %.2f] [%.2f, %.2f, %.2f, %.2f] [%.2f, %.2f, %.2f, %.2f] [%.2f, %.2f, %.2f, %.2f], vp: %dx%d",
                matrixElements[0], matrixElements[1], matrixElements[2], matrixElements[3],
                matrixElements[4], matrixElements[5], matrixElements[6], matrixElements[7],
                matrixElements[8], matrixElements[9], matrixElements[10], matrixElements[11],
                matrixElements[12], matrixElements[13], matrixElements[14], matrixElements[15],
                width, height);
        }
    }

    session->layerManager.RenderLiveInking(width, height, matrixElements);

    if (matrixElements != nullptr) {
        env->ReleaseFloatArrayElements(surfaceTransform, matrixElements, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderDry(
    JNIEnv* env, jclass /*clazz*/,
    jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    jfloat* matrixElements = nullptr;
    if (surfaceTransform != nullptr && env->GetArrayLength(surfaceTransform) >= 16) {
        matrixElements = env->GetFloatArrayElements(surfaceTransform, nullptr);
    }

    session->layerManager.RenderDocumentCanvas(width, height, matrixElements);
    session->layerManager.RenderImGui(width, height, matrixElements, /*isSdlHost=*/false);

    if (matrixElements != nullptr) {
        env->ReleaseFloatArrayElements(surfaceTransform, matrixElements, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeClearCanvas(
    JNIEnv* /*env*/, jclass /*clazz*/, jlong handle)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    session->layerManager.ClearDocument();
}

JNIEXPORT jboolean JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeTouch(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jlong handle, jfloat x, jfloat y, jint action)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return JNI_FALSE;

    // Convert Android MotionEvent actions: 0=ACTION_DOWN, 1=ACTION_UP, 2=ACTION_MOVE, 3=ACTION_CANCEL
    Folio::V2::InputPhase phase = Folio::V2::InputPhase::Move;
    if (action == 0) {
        phase = Folio::V2::InputPhase::Down;
    } else if (action == 1) {
        phase = Folio::V2::InputPhase::Up;
    } else if (action == 3) {
        phase = Folio::V2::InputPhase::Cancel;
    }

    bool captured = session->layerManager.HandleTouchForImGui(x, y, phase);
    return captured ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
