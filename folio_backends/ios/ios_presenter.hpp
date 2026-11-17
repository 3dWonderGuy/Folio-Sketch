#pragma once
/**
 * =========================================================================================
 * @file folio_backends/ios/ios_presenter.hpp
 * @brief Apple iOS / iPadOS Low-Latency Surface Presenter (Metal CAMetalLayer Scaffold)
 * =========================================================================================
 *
 * Implements ISurfacePresenter for iOS / iPadOS. Designed to bind to a CAMetalLayer
 * drawable pipeline with CADisplayLink synchronization and 240Hz Apple Pencil
 * coalesced / predicted touch delivery.
 */

#include "folio_backends/common/surface_presenter.hpp"

namespace Folio::Backends {

/**
 * @class IosPresenter
 * @brief Apple iOS / iPadOS low-latency surface presenter.
 */
class IosPresenter final : public ISurfacePresenter {
public:
    IosPresenter() = default;
    ~IosPresenter() override;

    IosPresenter(const IosPresenter&) = delete;
    IosPresenter& operator=(const IosPresenter&) = delete;

    /**
     * @brief Initializes presenter with an optional CAMetalLayer* / UIView* handle.
     */
    bool Init(void* metalLayerOrView = nullptr) override;

    /**
     * @brief Releases iOS presentation handles and state.
     */
    void Shutdown() override;

    /**
     * @brief Prepares drawable/command encoding for immediate wet stroke presentation.
     */
    bool BeginWetDraw(int* outWidth = nullptr, int* outHeight = nullptr) override;

    /**
     * @brief Commits wet stroke presentation to the active display drawable.
     */
    void EndWetDraw() const override;

    /**
     * @brief Clears the active iOS presentation surface to the specified RGBA color.
     */
    void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f) override;

    /**
     * @brief Updates drawable pixel dimensions after rotation or split-view resize.
     */
    void Resize(int width, int height) noexcept {
        m_width = width;
        m_height = height;
    }

    [[nodiscard]] bool IsInitialized() const noexcept override { return m_initialized; }
    [[nodiscard]] int GetWidth() const noexcept override { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept override { return m_height; }
    [[nodiscard]] BackendPlatform GetPlatform() const noexcept override { return BackendPlatform::iOS; }
    [[nodiscard]] CoordinateConvention GetCoordinateConvention() const noexcept override {
        return CoordinateConvention::DesktopStandard;
    }

private:
    void* m_nativeHandle = nullptr; ///< Reserved for CAMetalLayer* / MTLCommandQueue* bridge
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
};

} // namespace Folio::Backends
