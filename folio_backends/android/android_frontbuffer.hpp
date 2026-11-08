#pragma once
/**
 * =========================================================================================
 * @file folio_backends/android/android_frontbuffer.hpp
 * @brief Android Hardware Front-Buffered Presentation Wrapper
 * =========================================================================================
 *
 * Provides a C++ wrapper for Android single-buffered hardware presentation (direct scanout).
 * Bypasses SurfaceFlinger multi-buffer presentation latency for live wet inking strokes.
 */

#include <cstdint>

struct ANativeWindow;

namespace Folio::Backends {

/**
 * @class AndroidFrontBuffer
 * @brief Hardware direct-to-scanout single-buffered EGL surface presentation manager.
 */
class AndroidFrontBuffer {
public:
    AndroidFrontBuffer();
    ~AndroidFrontBuffer();

    AndroidFrontBuffer(const AndroidFrontBuffer&) = delete;
    AndroidFrontBuffer& operator=(const AndroidFrontBuffer&) = delete;

    /**
     * @brief Initializes single-buffered EGL surface on the provided native window.
     * @param window Target ANativeWindow pointer.
     * @return True if single-buffered / front-buffered surface was initialized.
     */
    bool Init(ANativeWindow* window);

    /**
     * @brief Releases EGL surface and context resources.
     */
    void Shutdown();

    /**
     * @brief Binds the front-buffer surface and sets up viewport for live wet drawing.
     * @param outWidth Optional output for surface width.
     * @param outHeight Optional output for surface height.
     * @return True if front-buffer surface is active.
     */
    bool BeginWetDraw(int* outWidth = nullptr, int* outHeight = nullptr);

    /**
     * @brief Immediately flushes rendering commands directly to display scanout without swapchain delay.
     */
    void EndWetDraw() const;

    /**
     * @brief Clears the front-buffer surface to transparent.
     */
    void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f);

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }

private:
    void* m_display = nullptr;  // EGLDisplay
    void* m_surface = nullptr;  // EGLSurface
    void* m_context = nullptr;  // EGLContext
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    bool m_ownsContext = false;
};

} // namespace Folio::Backends
