/**
 * =========================================================================================
 * @file folio_backends/sdl_gpu/sdl_gpu_presenter.cpp
 * @brief SDL3 GPU (Vulkan / D3D12 / Metal) Low-Latency Surface Presenter & Capsule Renderer
 * =========================================================================================
 */

#include "folio_backends/sdl_gpu/sdl_gpu_presenter.hpp"
#include "folio_backends/sdl_gpu/capsule_shaders_spv.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace Folio::Backends {

namespace {

struct VertexUniforms {
    float uTransform[4];
    float uViewport[2];
    float uPad[2];
};

struct FragmentUniforms {
    float uTransform[4];
    float uGrain[2];
    float uPad[2];
};

constexpr float kUnitQuad[12] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f,  1.0f
};

} // namespace

SdlGpuPresenter::~SdlGpuPresenter() {
    Shutdown();
}

bool SdlGpuPresenter::Init(void* sdlWindow) {
    return InitWithDevice(static_cast<SDL_Window*>(sdlWindow), nullptr);
}

bool SdlGpuPresenter::InitWithDevice(SDL_Window* sdlWindow, SDL_GPUDevice* externalDevice) {
    Shutdown();

    m_window = sdlWindow;
    if (!m_window) return false;

    if (externalDevice) {
        m_device = externalDevice;
        m_ownsDevice = false;
    } else {
        m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
        m_ownsDevice = true;
        if (!m_device) {
            std::cerr << "[SdlGpuPresenter] SDL_CreateGPUDevice failed: " << SDL_GetError() << std::endl;
            return false;
        }
        if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
            std::cerr << "[SdlGpuPresenter] SDL_ClaimWindowForGPUDevice failed: " << SDL_GetError() << std::endl;
            Shutdown();
            return false;
        }
    }

    SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    SetPresentModeLowLatency(true);

    if (!CreatePipeline()) {
        std::cerr << "[SdlGpuPresenter] CreatePipeline failed" << std::endl;
        Shutdown();
        return false;
    }

    // Create static unit quad vertex buffer
    SDL_GPUBufferCreateInfo qInfo{};
    qInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    qInfo.size = sizeof(kUnitQuad);
    m_quadVbo = SDL_CreateGPUBuffer(m_device, &qInfo);
    if (!m_quadVbo) {
        Shutdown();
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tInfo{};
    tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tInfo.size = sizeof(kUnitQuad);
    SDL_GPUTransferBuffer* qTransfer = SDL_CreateGPUTransferBuffer(m_device, &tInfo);
    if (!qTransfer) {
        Shutdown();
        return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(m_device, qTransfer, false);
    std::memcpy(mapped, kUnitQuad, sizeof(kUnitQuad));
    SDL_UnmapGPUTransferBuffer(m_device, qTransfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{ qTransfer, 0 };
    SDL_GPUBufferRegion dst{ m_quadVbo, 0, sizeof(kUnitQuad) };
    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(m_device, qTransfer);

    EnsureInstanceBuffer(4096);

    m_initialized = true;
    return true;
}

bool SdlGpuPresenter::CreatePipeline() {
    SDL_GPUShaderCreateInfo vInfo{};
    vInfo.code = SdlGpuShaders::kCapsuleVertSpv;
    vInfo.code_size = SdlGpuShaders::kCapsuleVertSpvSize;
    vInfo.entrypoint = "main";
    vInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vInfo.num_uniform_buffers = 1;
    SDL_GPUShader* vs = SDL_CreateGPUShader(m_device, &vInfo);

    SDL_GPUShaderCreateInfo fInfo{};
    fInfo.code = SdlGpuShaders::kCapsuleFragSpv;
    fInfo.code_size = SdlGpuShaders::kCapsuleFragSpvSize;
    fInfo.entrypoint = "main";
    fInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fInfo.num_uniform_buffers = 1;
    SDL_GPUShader* fs = SDL_CreateGPUShader(m_device, &fInfo);

    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(m_device, vs);
        if (fs) SDL_ReleaseGPUShader(m_device, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vBindings[2]{};
    vBindings[0].slot = 0;
    vBindings[0].pitch = 2 * sizeof(float);
    vBindings[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vBindings[0].instance_step_rate = 0;

    vBindings[1].slot = 1;
    vBindings[1].pitch = sizeof(CapsuleInstance);
    vBindings[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    vBindings[1].instance_step_rate = 0;

    SDL_GPUVertexAttribute vAttrs[6]{};
    // aQuadPos
    vAttrs[0] = SDL_GPUVertexAttribute{ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0 };
    // aPointA (pointA)
    vAttrs[1] = SDL_GPUVertexAttribute{ 1, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(CapsuleInstance, pointA)) };
    // aPointB (pointB)
    vAttrs[2] = SDL_GPUVertexAttribute{ 2, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(CapsuleInstance, pointB)) };
    // aRadii (radii)
    vAttrs[3] = SDL_GPUVertexAttribute{ 3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(CapsuleInstance, radii)) };
    // aColor (RGBA8 unorm)
    vAttrs[4] = SDL_GPUVertexAttribute{ 4, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<Uint32>(offsetof(CapsuleInstance, colorRgba)) };
    // aColorEnd (RGBA8 unorm)
    vAttrs[5] = SDL_GPUVertexAttribute{ 5, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<Uint32>(offsetof(CapsuleInstance, colorRgbaEnd)) };

    SDL_GPUColorTargetDescription colorDesc{};
    colorDesc.format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window);
    colorDesc.blend_state.enable_blend = true;
    colorDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pInfo{};
    pInfo.vertex_shader = vs;
    pInfo.fragment_shader = fs;
    pInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pInfo.vertex_input_state.vertex_buffer_descriptions = vBindings;
    pInfo.vertex_input_state.num_vertex_buffers = 2;
    pInfo.vertex_input_state.vertex_attributes = vAttrs;
    pInfo.vertex_input_state.num_vertex_attributes = 6;
    pInfo.target_info.color_target_descriptions = &colorDesc;
    pInfo.target_info.num_color_targets = 1;

    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pInfo);
    SDL_ReleaseGPUShader(m_device, vs);
    SDL_ReleaseGPUShader(m_device, fs);
    return m_pipeline != nullptr;
}

bool SdlGpuPresenter::EnsureInstanceBuffer(size_t instanceCount) {
    if (instanceCount <= m_instanceCapacity && m_instanceBuffer && m_transferBuffer) {
        return true;
    }
    const size_t newCap = std::max(instanceCount, std::max<size_t>(4096, m_instanceCapacity * 2));
    const Uint32 byteSize = static_cast<Uint32>(newCap * sizeof(CapsuleInstance));

    if (m_instanceBuffer) {
        SDL_ReleaseGPUBuffer(m_device, m_instanceBuffer);
        m_instanceBuffer = nullptr;
    }
    if (m_transferBuffer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_transferBuffer);
        m_transferBuffer = nullptr;
    }

    SDL_GPUBufferCreateInfo bInfo{};
    bInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bInfo.size = byteSize;
    m_instanceBuffer = SDL_CreateGPUBuffer(m_device, &bInfo);

    SDL_GPUTransferBufferCreateInfo tInfo{};
    tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tInfo.size = byteSize;
    m_transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &tInfo);

    if (!m_instanceBuffer || !m_transferBuffer) {
        m_instanceCapacity = 0;
        return false;
    }
    m_instanceCapacity = newCap;
    return true;
}

void SdlGpuPresenter::Shutdown() {
    if (m_device) {
        SDL_WaitForGPUIdle(m_device);
        DestroyDocTexture();
        if (m_transferBuffer) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_transferBuffer);
            m_transferBuffer = nullptr;
        }
        if (m_instanceBuffer) {
            SDL_ReleaseGPUBuffer(m_device, m_instanceBuffer);
            m_instanceBuffer = nullptr;
        }
        if (m_quadVbo) {
            SDL_ReleaseGPUBuffer(m_device, m_quadVbo);
            m_quadVbo = nullptr;
        }
        if (m_pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
            m_pipeline = nullptr;
        }
        if (m_ownsDevice) {
            if (m_window) {
                SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
            }
            SDL_DestroyGPUDevice(m_device);
        }
        m_device = nullptr;
    }
    m_window = nullptr;
    m_ownsDevice = true;
    m_instanceCapacity = 0;
    m_width = 0;
    m_height = 0;
    m_initialized = false;
    m_wasLiveInking = false;
}

bool SdlGpuPresenter::EnsureDocTexture(int width, int height) {
    if (!m_device || !m_window || width <= 0 || height <= 0) return false;
    if (m_docTex && m_docW == width && m_docH == height) return true;

    DestroyDocTexture();

    SDL_GPUTextureCreateInfo tInfo{};
    tInfo.type = SDL_GPU_TEXTURETYPE_2D;
    tInfo.format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window);
    tInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tInfo.width = static_cast<Uint32>(width);
    tInfo.height = static_cast<Uint32>(height);
    tInfo.layer_count_or_depth = 1;
    tInfo.num_levels = 1;

    m_docTex = SDL_CreateGPUTexture(m_device, &tInfo);
    if (!m_docTex) return false;

    m_docW = width;
    m_docH = height;
    m_docTexValid = false;
    return true;
}

void SdlGpuPresenter::DestroyDocTexture() {
    if (m_device && m_docTex) {
        SDL_ReleaseGPUTexture(m_device, m_docTex);
        m_docTex = nullptr;
    }
    m_docW = 0;
    m_docH = 0;
    m_docTexValid = false;
}

bool SdlGpuPresenter::NeedsDocRefresh(bool docDirty,
                                      bool uiDirty,
                                      bool hasActiveVideo,
                                      bool isLiveInking,
                                      double refreshIntervalMs) const {
    if (!m_docTexValid || m_docW != m_width || m_docH != m_height) {
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

bool SdlGpuPresenter::BeginDocPass(SDL_GPUCommandBuffer* /*cmd*/, int* outWidth, int* outHeight) {
    if (!m_initialized || !m_window) return false;
    SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    if (!EnsureDocTexture(m_width, m_height)) return false;
    if (outWidth) *outWidth = m_width;
    if (outHeight) *outHeight = m_height;
    return true;
}

void SdlGpuPresenter::EndDocPass() {
    m_lastDocRenderTime = std::chrono::steady_clock::now();
    m_docTexValid = true;
}

void SdlGpuPresenter::BlitDocToScreen(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapTex, uint32_t swW, uint32_t swH) {
    if (!cmd || !swapTex || !m_docTex || m_docW <= 0 || m_docH <= 0) return;

    SDL_GPUBlitInfo blit{};
    blit.source.texture = m_docTex;
    blit.source.w = static_cast<Uint32>(m_docW);
    blit.source.h = static_cast<Uint32>(m_docH);
    blit.destination.texture = swapTex;
    blit.destination.w = swW;
    blit.destination.h = swH;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(cmd, &blit);
}

bool SdlGpuPresenter::SetPresentModeLowLatency(bool enableLowLatency) {
    if (!m_device || !m_window) return false;
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (enableLowLatency) {
        if (SDL_WindowSupportsGPUPresentMode(m_device, m_window, SDL_GPU_PRESENTMODE_MAILBOX)) {
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
            m_activePresentMode = 2;
        } else if (SDL_WindowSupportsGPUPresentMode(m_device, m_window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            m_activePresentMode = 1;
        } else {
            m_activePresentMode = 0;
        }
    } else {
        m_activePresentMode = 0;
    }
    return SDL_SetGPUSwapchainParameters(
        m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

void SdlGpuPresenter::SetLiveInkingState(bool isLiveInking) {
    if (isLiveInking != m_wasLiveInking) {
        SetPresentModeLowLatency(isLiveInking);
        m_wasLiveInking = isLiveInking;
    }
}

bool SdlGpuPresenter::BeginWetDraw(int* outWidth, int* outHeight) {
    if (!m_initialized || !m_window) return false;
    SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    if (outWidth) *outWidth = m_width;
    if (outHeight) *outHeight = m_height;
    return true;
}

void SdlGpuPresenter::EndWetDraw() const {}

void SdlGpuPresenter::Clear(float, float, float, float) {}

bool SdlGpuPresenter::RenderFrame(std::span<const CapsuleInstance> committedInstances,
                                  std::span<const CapsuleInstance> wetInstances,
                                  const CanvasTransform& transform,
                                  float grainStrength,
                                  float grainDensity,
                                  float* outEngineDrawMs,
                                  float* outSwapWaitMs) {
    if (!m_initialized) return false;

    const auto tStart = std::chrono::steady_clock::now();
    SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);

    const size_t totalInstances = committedInstances.size() + wetInstances.size();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) return false;

    if (totalInstances > 0) {
        if (!EnsureInstanceBuffer(totalInstances)) {
            SDL_CancelGPUCommandBuffer(cmd);
            return false;
        }
        auto* dst = static_cast<CapsuleInstance*>(SDL_MapGPUTransferBuffer(m_device, m_transferBuffer, true));
        if (!committedInstances.empty()) {
            std::memcpy(dst, committedInstances.data(), committedInstances.size() * sizeof(CapsuleInstance));
        }
        if (!wetInstances.empty()) {
            std::memcpy(dst + committedInstances.size(), wetInstances.data(), wetInstances.size() * sizeof(CapsuleInstance));
        }
        SDL_UnmapGPUTransferBuffer(m_device, m_transferBuffer);

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{ m_transferBuffer, 0 };
        SDL_GPUBufferRegion region{ m_instanceBuffer, 0, static_cast<Uint32>(totalInstances * sizeof(CapsuleInstance)) };
        SDL_UploadToGPUBuffer(copyPass, &src, &region, true);
        SDL_EndGPUCopyPass(copyPass);
    }

    SDL_GPUTexture* swapTex = nullptr;
    Uint32 swW = 0, swH = 0;
    const auto tBeforeSwap = std::chrono::steady_clock::now();
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, m_window, &swapTex, &swW, &swH)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    const auto tAfterAcquire = std::chrono::steady_clock::now();

    if (swapTex) {
        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = swapTex;
        colorTarget.clear_color = { 0.11f, 0.12f, 0.14f, 1.0f };
        colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);
        if (totalInstances > 0) {
            SDL_BindGPUGraphicsPipeline(rp, m_pipeline);

            SDL_GPUBufferBinding bindings[2]{
                { m_quadVbo, 0 },
                { m_instanceBuffer, 0 }
            };
            SDL_BindGPUVertexBuffers(rp, 0, bindings, 2);

            VertexUniforms vu{};
            vu.uTransform[0] = transform.panX;
            vu.uTransform[1] = transform.panY;
            vu.uTransform[2] = transform.zoom;
            vu.uViewport[0] = static_cast<float>(swW);
            vu.uViewport[1] = static_cast<float>(swH);
            SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));

            FragmentUniforms fu{};
            fu.uTransform[0] = transform.panX;
            fu.uTransform[1] = transform.panY;
            fu.uTransform[2] = transform.zoom;
            fu.uGrain[0] = grainStrength;
            fu.uGrain[1] = grainDensity;
            SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));

            SDL_DrawGPUPrimitives(rp, 6, static_cast<Uint32>(totalInstances), 0, 0);
        }
        SDL_EndGPURenderPass(rp);
    }

    const auto tBeforeSubmit = std::chrono::steady_clock::now();
    SDL_SubmitGPUCommandBuffer(cmd);
    const auto tEnd = std::chrono::steady_clock::now();

    if (outEngineDrawMs) {
        *outEngineDrawMs = std::chrono::duration<float, std::milli>(tBeforeSwap - tStart).count()
                         + std::chrono::duration<float, std::milli>(tBeforeSubmit - tAfterAcquire).count();
    }
    if (outSwapWaitMs) {
        *outSwapWaitMs = std::chrono::duration<float, std::milli>(tAfterAcquire - tBeforeSwap).count()
                       + std::chrono::duration<float, std::milli>(tEnd - tBeforeSubmit).count();
    }
    return true;
}

const char* SdlGpuPresenter::GetDriverName() const noexcept {
    return m_device ? SDL_GetGPUDeviceDriver(m_device) : "none";
}

const char* SdlGpuPresenter::GetPresentModeName() const noexcept {
    switch (m_activePresentMode) {
        case 2: return "MAILBOX";
        case 1: return "IMMEDIATE";
        default: return "VSYNC";
    }
}

} // namespace Folio::Backends
