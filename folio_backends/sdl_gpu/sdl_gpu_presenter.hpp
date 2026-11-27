#pragma once
/**
 * =========================================================================================
 * @file folio_backends/sdl_gpu/sdl_gpu_presenter.hpp
 * @brief SDL3 GPU (Vulkan / D3D12 / Metal) Low-Latency Surface Presenter & Capsule Renderer
 * =========================================================================================
 */

#include <chrono>
#include <span>
#include "folio_backends/common/gl_capsule_renderer.hpp"
#include "folio_backends/common/surface_presenter.hpp"

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUTexture;

namespace Folio::Backends {

class SdlGpuPresenter final : public ISurfacePresenter {
public:
    SdlGpuPresenter() = default;
    ~SdlGpuPresenter() override;

    SdlGpuPresenter(const SdlGpuPresenter&) = delete;
    SdlGpuPresenter& operator=(const SdlGpuPresenter&) = delete;

    bool Init(void* sdlWindow = nullptr) override;
    void Shutdown() override;

    bool BeginWetDraw(int* outWidth = nullptr, int* outHeight = nullptr) override;
    void EndWetDraw() const override;
    void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f) override;

    bool SetPresentModeLowLatency(bool enableLowLatency);
    void SetLiveInkingState(bool isLiveInking);

    bool RenderFrame(std::span<const CapsuleInstance> committedInstances,
                     std::span<const CapsuleInstance> wetInstances,
                     const CanvasTransform& transform,
                     float grainStrength = 0.0f,
                     float grainDensity = 0.25f,
                     float* outEngineDrawMs = nullptr,
                     float* outSwapWaitMs = nullptr);

    [[nodiscard]] bool IsInitialized() const noexcept override { return m_initialized; }
    [[nodiscard]] int GetWidth() const noexcept override { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept override { return m_height; }
    [[nodiscard]] BackendPlatform GetPlatform() const noexcept override { return BackendPlatform::Desktop; }
    [[nodiscard]] CoordinateConvention GetCoordinateConvention() const noexcept override {
        return CoordinateConvention::DesktopStandard;
    }
    [[nodiscard]] const char* GetDriverName() const noexcept;
    [[nodiscard]] const char* GetPresentModeName() const noexcept;

private:
    bool CreatePipeline();
    bool EnsureInstanceBuffer(size_t instanceCount);

    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUBuffer* m_quadVbo = nullptr;
    SDL_GPUBuffer* m_instanceBuffer = nullptr;
    SDL_GPUTransferBuffer* m_transferBuffer = nullptr;
    size_t m_instanceCapacity = 0;

    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    bool m_wasLiveInking = false;
    int m_activePresentMode = 0; // 0=VSYNC, 1=IMMEDIATE, 2=MAILBOX
};

} // namespace Folio::Backends
