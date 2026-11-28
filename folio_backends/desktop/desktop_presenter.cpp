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
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
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
    DestroyDocFbo();
    m_window = nullptr;
    m_width = 0;
    m_height = 0;
    m_initialized = false;
    m_wasLiveInking = false;
}

bool DesktopPresenter::EnsureDocFbo(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (m_docFbo != 0 && m_docW == width && m_docH == height) return true;

    DestroyDocFbo();

    GLuint fbo = 0;
    GLuint tex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_docFbo = fbo;
    m_docTex = tex;
    m_docW = width;
    m_docH = height;
    m_docFboValid = false;
    return m_docFbo != 0;
}

void DesktopPresenter::DestroyDocFbo() {
    if (m_docTex != 0) {
        GLuint tex = m_docTex;
        glDeleteTextures(1, &tex);
        m_docTex = 0;
    }
    if (m_docFbo != 0) {
        GLuint fbo = m_docFbo;
        glDeleteFramebuffers(1, &fbo);
        m_docFbo = 0;
    }
    m_docW = 0;
    m_docH = 0;
    m_docFboValid = false;
}

void DesktopPresenter::SetLiveInkingState(bool isLiveInking) {
#if FOLIO_DESKTOP_HAS_SDL3
    if (isLiveInking != m_wasLiveInking) {
        SDL_GL_SetSwapInterval(isLiveInking ? 0 : 1);
        m_wasLiveInking = isLiveInking;
    }
#else
    (void)isLiveInking;
#endif
}

bool DesktopPresenter::NeedsDocRefresh(bool docDirty,
                                       bool uiDirty,
                                       bool hasActiveVideo,
                                       bool isLiveInking,
                                       double refreshIntervalMs) const {
    if (!m_docFboValid || m_docW != m_width || m_docH != m_height) {
        return true;
    }
    if (!docDirty && !uiDirty && !hasActiveVideo) {
        return false;
    }
    if (!isLiveInking) {
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(now - m_lastDocRenderTime).count();
    return elapsedMs >= refreshIntervalMs;
}

bool DesktopPresenter::BeginDocPass(int* outWidth, int* outHeight) {
    if (!m_initialized) return false;
#if FOLIO_DESKTOP_HAS_SDL3
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    }
#endif
    if (!EnsureDocFbo(m_width, m_height)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, m_docFbo);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_SCISSOR_TEST);

    if (outWidth) *outWidth = m_width;
    if (outHeight) *outHeight = m_height;
    return true;
}

void DesktopPresenter::EndDocPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_lastDocRenderTime = std::chrono::steady_clock::now();
    m_docFboValid = true;
}

void DesktopPresenter::BlitDocToScreen() {
    if (!m_initialized) return;
#if FOLIO_DESKTOP_HAS_SDL3
    if (m_window != nullptr) {
        SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_SCISSOR_TEST);

    if (m_docFbo != 0 && m_docW > 0 && m_docH > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_docFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, m_docW, m_docH, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
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
