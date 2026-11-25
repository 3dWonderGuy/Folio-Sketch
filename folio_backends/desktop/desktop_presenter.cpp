/**
 * =========================================================================================
 * @file folio_backends/desktop/desktop_presenter.cpp
 * @brief Shared Desktop (Linux & Windows) Low-Latency OpenGL Surface Presenter
 * =========================================================================================
 */

#include "folio_backends/desktop/desktop_presenter.hpp"

#if !defined(__ANDROID__) && !defined(__APPLE__)

#if defined(_WIN32)
#include "inspectorV2.0/gl_loader.hpp"
#endif

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#define FOLIO_DESKTOP_HAS_SDL3 1
#else
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
#define FOLIO_DESKTOP_HAS_SDL3 0
#endif

namespace Folio::Backends {

DesktopPresenter::~DesktopPresenter() {
    Shutdown();
}

bool DesktopPresenter::Init(void* sdlWindow) {
    Shutdown();

    m_window = static_cast<SDL_Window*>(sdlWindow);

#if FOLIO_DESKTOP_HAS_SDL3
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    }
#endif

    if (m_width <= 0 || m_height <= 0) {
        GLint vp[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, vp);
        m_width = vp[2];
        m_height = vp[3];
    }

    SetSwapIntervalLowLatency(true);
    m_initialized = true;
    return true;
}

void DesktopPresenter::Shutdown() {
    m_window = nullptr;
    m_width = 0;
    m_height = 0;
    m_initialized = false;
}

bool DesktopPresenter::BeginWetDraw(int* outWidth, int* outHeight) {
    if (!m_initialized) return false;

#if FOLIO_DESKTOP_HAS_SDL3
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    }
#endif

    if (m_width > 0 && m_height > 0) {
        glViewport(0, 0, m_width, m_height);
    }

    if (outWidth) *outWidth = m_width;
    if (outHeight) *outHeight = m_height;
    return true;
}

void DesktopPresenter::EndWetDraw() const {
    if (!m_initialized) return;
#if FOLIO_DESKTOP_HAS_SDL3
    if (m_window != nullptr) {
        SDL_GL_SwapWindow(m_window);
        return;
    }
#endif
    glFlush();
}

void DesktopPresenter::Clear(float r, float g, float b, float a) {
    if (!BeginWetDraw()) return;
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
}

bool DesktopPresenter::SetSwapIntervalLowLatency(bool enableAdaptiveOrImmediate) {
#if FOLIO_DESKTOP_HAS_SDL3
    if (!enableAdaptiveOrImmediate) {
        return SDL_GL_SetSwapInterval(1);
    }
    // Prefer adaptive vsync (-1); fall back to immediate presentation (0) if unsupported.
    if (SDL_GL_SetSwapInterval(-1)) {
        return true;
    }
    return SDL_GL_SetSwapInterval(0);
#else
    (void)enableAdaptiveOrImmediate;
    return false;
#endif
}

void RequestFrontBufferFlush() {}

void RequestAndroidFrontBufferFlush() {}

std::unique_ptr<ISurfacePresenter> CreatePlatformPresenter() {
    return std::make_unique<DesktopPresenter>();
}

} // namespace Folio::Backends

#endif // !__ANDROID__ && !__APPLE__
