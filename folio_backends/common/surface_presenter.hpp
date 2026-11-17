#pragma once
/**
 * =========================================================================================
 * @file folio_backends/common/surface_presenter.hpp
 * @brief Unified Cross-Platform Low-Latency Surface Presentation Interface
 * =========================================================================================
 *
 * Abstracts platform-specific presentation mechanics behind a single interface so that
 * shared engine/UI layers (e.g. LayerManager, FolioNote) operate uniformly across:
 *   - Desktop (Linux / Windows): SDL3 + OpenGL 3.3 (adaptive/immediate scanout + glFlush)
 *   - Android: EGL_SINGLE_BUFFER / GLFrontBufferedRenderer + BufferInfo.transform
 *   - iOS / iPadOS: Metal CAMetalLayer + Apple Pencil 240Hz presentation
 */

#include <cstdint>
#include <memory>
#include "folio_backends/common/display_surface_transform.hpp"

namespace Folio::Backends {

/**
 * @enum BackendPlatform
 * @brief Identifies the active platform presentation backend.
 */
enum class BackendPlatform : uint8_t {
    Desktop = 0, ///< Linux & Windows (OpenGL 3.3 + SDL3)
    Android,     ///< Android (GLES 3.0 + EGL_SINGLE_BUFFER / GLFrontBufferedRenderer)
    iOS          ///< Apple iOS / iPadOS (Metal CAMetalLayer / Apple Pencil 240Hz)
};

/**
 * @class ISurfacePresenter
 * @brief Unified low-latency surface presentation interface across platforms.
 */
class ISurfacePresenter {
public:
    virtual ~ISurfacePresenter() = default;

    /**
     * @brief Initializes the platform presentation surface using an optional native window/handle.
     */
    virtual bool Init(void* nativeWindowOrHandle = nullptr) = 0;

    /**
     * @brief Releases surface and platform presenter resources.
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Prepares the presentation surface and viewport for immediate wet ink rendering.
     */
    virtual bool BeginWetDraw(int* outWidth = nullptr, int* outHeight = nullptr) = 0;

    /**
     * @brief Flushes pending wet ink rendering commands for immediate display presentation.
     */
    virtual void EndWetDraw() const = 0;

    /**
     * @brief Clears the presentation surface to the specified RGBA color.
     */
    virtual void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f) = 0;

    [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;
    [[nodiscard]] virtual int GetWidth() const noexcept = 0;
    [[nodiscard]] virtual int GetHeight() const noexcept = 0;
    [[nodiscard]] virtual BackendPlatform GetPlatform() const noexcept = 0;
    [[nodiscard]] virtual CoordinateConvention GetCoordinateConvention() const noexcept = 0;
    [[nodiscard]] virtual const float* GetSurfaceTransform() const noexcept {
        return kIdentityMatrix4x4.data();
    }
};

/**
 * @brief Requests an immediate front-buffer flush on platforms with asynchronous scanout
 *        bridges (Android), or acts as a no-op on synchronous desktop hosts.
 */
void RequestFrontBufferFlush();

/**
 * @brief Backward-compatible alias used by Android JNI / SDL host loop.
 */
void RequestAndroidFrontBufferFlush();

/**
 * @brief Creates the platform-native presenter instance for the current target OS.
 */
[[nodiscard]] std::unique_ptr<ISurfacePresenter> CreatePlatformPresenter();

} // namespace Folio::Backends
