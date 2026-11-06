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
    jboolean isDown, jboolean isUp, jboolean isStylus)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    Folio::V2::PointerPacket pkt;
    pkt.device = isStylus ? Folio::V2::DeviceType::Stylus : Folio::V2::DeviceType::Touch;
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

} // extern "C"
