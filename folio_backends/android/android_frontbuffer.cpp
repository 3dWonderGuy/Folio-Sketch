/**
 * =========================================================================================
 * @file folio_backends/android/android_frontbuffer.cpp
 * @brief Android Hardware Front-Buffered Presentation Wrapper
 * =========================================================================================
 */

#include "folio_backends/android/android_frontbuffer.hpp"

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/log.h>

#ifndef EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID
#define EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID 0x314C
#endif

#define LOG_TAG "FolioFrontBuffer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#else
#include <iostream>
#endif

namespace Folio::Backends {

AndroidFrontBuffer::AndroidFrontBuffer() = default;
AndroidFrontBuffer::~AndroidFrontBuffer() {
    Shutdown();
}

bool AndroidFrontBuffer::Init(ANativeWindow* window) {
    if (!window) return false;
    Shutdown();
    m_window = window;

#if defined(__ANDROID__)
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    if (!eglInitialize(display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    0,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs <= 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    // Request direct single-buffered hardware scanout surface
    const EGLint surfaceAttribs[] = {
        EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER,
        EGL_NONE
    };

    EGLSurface surface = eglCreateWindowSurface(display, config, window, surfaceAttribs);
    if (surface == EGL_NO_SURFACE) {
        // Fallback to default window surface
        surface = eglCreateWindowSurface(display, config, window, nullptr);
    }

    if (surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed");
        return false;
    }

    // Enable front buffer auto refresh (bypasses swapchain compositor latency)
    eglSurfaceAttrib(display, surface, EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID, 1);

    // Share active context if one exists, otherwise create dedicated GLES3 context
    EGLContext currentContext = eglGetCurrentContext();
    EGLContext context = EGL_NO_CONTEXT;
    if (currentContext != EGL_NO_CONTEXT) {
        context = currentContext;
        m_ownsContext = false;
    } else {
        const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
        m_ownsContext = true;
    }

    if (context == EGL_NO_CONTEXT) {
        LOGE("Failed to obtain EGL context for front buffer");
        eglDestroySurface(display, surface);
        return false;
    }

    m_display = display;
    m_surface = surface;
    m_context = context;

    eglQuerySurface(display, surface, EGL_WIDTH, &m_width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &m_height);

    m_initialized = true;
    LOGI("AndroidFrontBuffer initialized (%dx%d, single-buffer auto-refresh enabled)", m_width, m_height);
    return true;
#else
    m_width = 1920;
    m_height = 1080;
    m_initialized = true;
    return true;
#endif
}

void AndroidFrontBuffer::Shutdown() {
    if (!m_initialized) return;

#if defined(__ANDROID__)
    EGLDisplay display = static_cast<EGLDisplay>(m_display);
    EGLSurface surface = static_cast<EGLSurface>(m_surface);
    EGLContext context = static_cast<EGLContext>(m_context);

    if (display != EGL_NO_DISPLAY) {
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        if (m_ownsContext && context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
    }
#endif

    m_display = nullptr;
    m_surface = nullptr;
    m_context = nullptr;
    m_window = nullptr;
    m_width = 0;
    m_height = 0;
    m_initialized = false;
    m_ownsContext = false;
}

bool AndroidFrontBuffer::BeginWetDraw(int* outWidth, int* outHeight) {
    if (!m_initialized) return false;

#if defined(__ANDROID__)
    EGLDisplay display = static_cast<EGLDisplay>(m_display);
    EGLSurface surface = static_cast<EGLSurface>(m_surface);
    EGLContext context = static_cast<EGLContext>(m_context);

    if (!eglMakeCurrent(display, surface, surface, context)) {
        return false;
    }

    eglQuerySurface(display, surface, EGL_WIDTH, &m_width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &m_height);

    glViewport(0, 0, m_width, m_height);
#endif

    if (outWidth) *outWidth = m_width;
    if (outHeight) *outHeight = m_height;
    return true;
}

void AndroidFrontBuffer::EndWetDraw() {
    if (!m_initialized) return;

#if defined(__ANDROID__)
    // In single-buffer direct scanout mode, glFlush pushes directly to display controller
    glFlush();
#endif
}

void AndroidFrontBuffer::Clear(float r, float g, float b, float a) {
    if (!BeginWetDraw()) return;

#if defined(__ANDROID__)
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
#endif
}

} // namespace Folio::Backends
