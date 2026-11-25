/**
 * =========================================================================================
 * @file folio_backends/android/folio_android_jni.cpp
 * @brief Android JNI Bridge for Zero-Latency Front-Buffer Scanout
 * =========================================================================================
 *
 * All input ingestion, stroke physics, document state, and Dear ImGui rendering
 * belong exclusively to Folio::V2::AppBrain::Instance().GetLayerManager().
 * ALL rendering (wet ink, dry document, ImGui HUD) is pushed directly to the front buffer.
 */

#include <jni.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <SDL3/SDL.h>

#include "folio_backends/android/android_frontbuffer.hpp"

#define LOG_TAG "FolioSketchNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_SWAP(...) __android_log_print(ANDROID_LOG_VERBOSE, "FolioSwap", __VA_ARGS__)

namespace {

static JavaVM* g_jvm = nullptr;
static jclass g_inkViewClass = nullptr;
static jmethodID g_requestFrontBufferMethod = nullptr;
static Folio::Backends::AndroidHostCallbacks g_hostCallbacks{};

struct FolioAndroidSession {
    uint64_t wetFlushCount = 0;
};

} // namespace

namespace Folio::Backends {

void SetAndroidHostCallbacks(const AndroidHostCallbacks& callbacks) {
    g_hostCallbacks = callbacks;
}

const AndroidHostCallbacks& GetAndroidHostCallbacks() {
    return g_hostCallbacks;
}

void RequestAndroidFrontBufferFlush() {
    if (!g_jvm || !g_inkViewClass || !g_requestFrontBufferMethod) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    jint status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    } else if (status != JNI_OK) {
        return;
    }

    env->CallStaticVoidMethod(g_inkViewClass, g_requestFrontBufferMethod);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

} // namespace Folio::Backends

extern "C" {

JNIEXPORT jlong JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeCreateEngine(JNIEnv* env, jclass /*clazz*/) {
    if (!g_jvm) {
        env->GetJavaVM(&g_jvm);
    }
    if (!g_inkViewClass) {
        jclass localCls = env->FindClass("org/foliosketch/android/FolioInkView");
        if (localCls) {
            g_inkViewClass = static_cast<jclass>(env->NewGlobalRef(localCls));
            g_requestFrontBufferMethod = env->GetStaticMethodID(
                g_inkViewClass, "requestFrontBufferRenderFromNative", "()V");
            env->DeleteLocalRef(localCls);
        }
    }

    auto* session = new FolioAndroidSession();
    LOGI("Created native Folio front-buffer session: %p", session);
    return reinterpret_cast<jlong>(session);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeDestroyEngine(JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (session) {
        delete session;
        LOGI("Destroyed native Folio front-buffer session");
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

    const auto& cb = Folio::Backends::GetAndroidHostCallbacks();
    if (cb.onIngestPacket) {
        cb.onIngestPacket(x, y, pressure, tiltX, tiltY, timeSec,
                          isDown != JNI_FALSE, isUp != JNI_FALSE, isStylus != JNI_FALSE,
                          static_cast<int>(pointerId));
    }

    if (isUp) {
        // Wake SDL event loop if sleeping
        SDL_Event userEv{};
        userEv.type = SDL_EVENT_USER;
        SDL_PushEvent(&userEv);
    }
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

    const auto& cb = Folio::Backends::GetAndroidHostCallbacks();
    if (cb.onRenderFrame) {
        cb.onRenderFrame(width, height, matrixElements);
    }

    if (matrixElements != nullptr) {
        env->ReleaseFloatArrayElements(surfaceTransform, matrixElements, JNI_ABORT);
    }

    glFlush();
    session->wetFlushCount++;
    LOG_SWAP("Front-buffer full pass flushed (count: %llu)", (unsigned long long)session->wetFlushCount);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderDry(
    JNIEnv* env, jclass clazz,
    jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    // Forward any dry requests directly to the unified front-buffer pass
    Java_org_foliosketch_android_FolioNativeBridge_nativeRenderWet(env, clazz, handle, width, height, surfaceTransform);
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeClearCanvas(
    JNIEnv* /*env*/, jclass /*clazz*/, jlong handle)
{
    auto* session = reinterpret_cast<FolioAndroidSession*>(handle);
    if (!session) return;

    const auto& cb = Folio::Backends::GetAndroidHostCallbacks();
    if (cb.onClearCanvas) {
        cb.onClearCanvas();
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
}

JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeUpdateSurfaceTransform(
    JNIEnv* env, jclass /*clazz*/, jlong handle, jfloatArray surfaceTransform)
{
    (void)env;
    (void)handle;
    (void)surfaceTransform;
}

} // extern "C"
