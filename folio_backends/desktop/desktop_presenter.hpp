#pragma once
/**
 * =========================================================================================
 * @file folio_backends/desktop/desktop_presenter.hpp
 * @brief Shared Desktop (Linux & Windows) Low-Latency OpenGL Surface Presenter
 * =========================================================================================
 *
 * Implements ISurfacePresenter for Linux and Windows desktop hosts using SDL3 + OpenGL 3.3.
 * Supports adaptive/immediate swap interval tuning and direct glFlush presentation.
 */

#include "folio_backends/common/surface_presenter.hpp"

struct SDL_Window;

namespace Folio::Backends {

/**
 * @class DesktopPresenter
 * @brief Shared Linux & Windows low-latency OpenGL surface presenter.
 */
class DesktopPresenter final : public ISurfacePresenter {
public:
    DesktopPresenter() = default;
    ~DesktopPresenter() override;

    DesktopPresenter(const DesktopPresenter&) = delete;
    DesktopPresenter& operator=(const DesktopPresenter&) = delete;

    /**
     * @brief Initializes presenter against an active SDL_Window* (or current GL context).
     */
    bool Init(void* sdlWindow = nullptr) override;

    /**
     * @brief Releases desktop presenter state.
     */
    void Shutdown() override;

    /**
     * @brief Updates viewport dimensions and prepares for immediate wet drawing.
     */
    bool BeginWetDraw(int* outWidth = nullptr, int* outHeight = nullptr) override;

    /**
     * @brief Flushes pending OpenGL commands immediately for low-latency presentation.
     */
    void EndWetDraw() const override;

    /**
     * @brief Clears the active desktop framebuffer to the specified RGBA color.
     */
    void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f) override;

    /**
     * @brief Configures SDL GL swap interval for minimum latency (adaptive vsync -1, falling back to 0).
     */
    bool SetSwapIntervalLowLatency(bool enableAdaptiveOrImmediate = true);

    [[nodiscard]] bool IsInitialized() const noexcept override { return m_initialized; }
    [[nodiscard]] int GetWidth() const noexcept override { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept override { return m_height; }
    [[nodiscard]] BackendPlatform GetPlatform() const noexcept override { return BackendPlatform::Desktop; }
    [[nodiscard]] CoordinateConvention GetCoordinateConvention() const noexcept override {
        return CoordinateConvention::DesktopStandard;
    }

private:
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
};

} // namespace Folio::Backends
