/**
 * =========================================================================================
 * @file inspectorV2.0/main.cpp
 * @brief FolioNote Inspector V2.0 Desktop Host (SDL3 + OpenGL + Dear ImGui)
 * =========================================================================================
 */

#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include "inspectorV2.0/gl_loader.hpp"
#include "inspectorV2.0/app_brain.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include "inspectorV2.0/testbed/testbed_samples.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"
#include "folio_backends/common/surface_presenter.hpp"
#include "folio_backends/desktop/desktop_presenter.hpp"

namespace {

void ScreenToFb(SDL_Window* window, float sx, float sy, float& outX, float& outY) {
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    SDL_GetWindowSize(window, &winW, &winH);
    SDL_GetWindowSizeInPixels(window, &fbW, &fbH);
    float sxFactor = (winW > 0) ? (static_cast<float>(fbW) / static_cast<float>(winW)) : 1.0f;
    float syFactor = (winH > 0) ? (static_cast<float>(fbH) / static_cast<float>(winH)) : 1.0f;
    outX = sx * sxFactor;
    outY = sy * syFactor;
}

void ProcessSdlEvent(const SDL_Event& event, SDL_Window* window, Folio::V2::LayerManager& layerManager,
                     const ImGuiIO& io, bool& needsRedraw, bool& running) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_QUIT ||
        (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
         event.window.windowID == SDL_GetWindowID(window))) {
        running = false;
        return;
    }

    if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
        event.type == SDL_EVENT_WINDOW_RESIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        needsRedraw = true;
        return;
    }

    if (event.type == SDL_EVENT_PEN_AXIS) {
        constexpr float kDegToRad = 3.14159265f / 180.0f;
        auto& telem = const_cast<Folio::V2::LayerManager::StylusTelemetry&>(layerManager.GetStylusTelemetry());
        if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
            telem.pressure = std::clamp(event.paxis.value, 0.0f, 1.0f);
        } else if (event.paxis.axis == SDL_PEN_AXIS_XTILT) {
            telem.tiltX = std::clamp(event.paxis.value * kDegToRad, -1.570796f, 1.570796f);
        } else if (event.paxis.axis == SDL_PEN_AXIS_YTILT) {
            telem.tiltY = std::clamp(event.paxis.value * kDegToRad, -1.570796f, 1.570796f);
        }
        needsRedraw = true;
    } else if (event.type == SDL_EVENT_PEN_DOWN || event.type == SDL_EVENT_PEN_MOTION || event.type == SDL_EVENT_PEN_UP) {
        float rawX = (event.type == SDL_EVENT_PEN_MOTION) ? event.pmotion.x : event.ptouch.x;
        float rawY = (event.type == SDL_EVENT_PEN_MOTION) ? event.pmotion.y : event.ptouch.y;
        float fbX = rawX, fbY = rawY;
        ScreenToFb(window, rawX, rawY, fbX, fbY);

        Folio::V2::PointerPacket pkt;
        pkt.device = Folio::V2::DeviceType::Stylus;
        pkt.pointerId = 2;
        pkt.x = fbX;
        pkt.y = fbY;
        pkt.tiltX = layerManager.GetStylusTelemetry().tiltX;
        pkt.tiltY = layerManager.GetStylusTelemetry().tiltY;

        if (event.type == SDL_EVENT_PEN_DOWN) {
            pkt.phase = Folio::V2::InputPhase::Down;
            pkt.pressure = (layerManager.GetStylusTelemetry().pressure > 0.001f) ? layerManager.GetStylusTelemetry().pressure : 0.6f;
            pkt.timestampSec = static_cast<double>(event.ptouch.timestamp) * 1e-9;
        } else if (event.type == SDL_EVENT_PEN_MOTION) {
            pkt.phase = Folio::V2::InputPhase::Move;
            pkt.pressure = (layerManager.GetStylusTelemetry().pressure > 0.001f) ? layerManager.GetStylusTelemetry().pressure : 0.6f;
            pkt.timestampSec = static_cast<double>(event.pmotion.timestamp) * 1e-9;
        } else {
            pkt.phase = Folio::V2::InputPhase::Up;
            pkt.pressure = 0.0f;
            pkt.timestampSec = static_cast<double>(event.ptouch.timestamp) * 1e-9;
        }
        layerManager.ProcessPointer(pkt);
        needsRedraw = true;
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (!io.WantCaptureMouse) {
            float mouseX = 0.0f, mouseY = 0.0f;
            SDL_GetMouseState(&mouseX, &mouseY);
            float fbX = mouseX, fbY = mouseY;
            ScreenToFb(window, mouseX, mouseY, fbX, fbY);
            float zoomFactor = (event.wheel.y > 0) ? 1.15f : 0.87f;
            layerManager.GetInputManager().onCanvasZoom(fbX, fbY, zoomFactor);
            needsRedraw = true;
        }
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && (io.WantCaptureMouse || event.button.button != SDL_BUTTON_LEFT)) {
            return;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION && (io.WantCaptureMouse || !(event.motion.state & SDL_BUTTON_LMASK))) {
            return;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button != SDL_BUTTON_LEFT) {
            return;
        }

        float rawX = (event.type == SDL_EVENT_MOUSE_MOTION) ? event.motion.x : event.button.x;
        float rawY = (event.type == SDL_EVENT_MOUSE_MOTION) ? event.motion.y : event.button.y;
        float fbX = rawX, fbY = rawY;
        ScreenToFb(window, rawX, rawY, fbX, fbY);

        Folio::V2::PointerPacket pkt;
        pkt.device = Folio::V2::DeviceType::Mouse;
        pkt.pointerId = 1;
        pkt.x = fbX;
        pkt.y = fbY;

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            pkt.phase = Folio::V2::InputPhase::Down;
            pkt.pressure = 0.8f;
            pkt.timestampSec = static_cast<double>(event.button.timestamp) * 1e-9;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            pkt.phase = Folio::V2::InputPhase::Move;
            pkt.pressure = 0.8f;
            pkt.timestampSec = static_cast<double>(event.motion.timestamp) * 1e-9;
        } else {
            pkt.phase = Folio::V2::InputPhase::Up;
            pkt.pressure = 0.0f;
            pkt.timestampSec = static_cast<double>(event.button.timestamp) * 1e-9;
        }
        layerManager.ProcessPointer(pkt);
        needsRedraw = true;
    } else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE) {
            running = false;
        } else if (event.key.key == SDLK_C) {
            layerManager.ClearDocument();
            needsRedraw = true;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--diagnose") == 0) {
            std::cout << "[FolioInspectorV2] Running headless diagnostic suite...\n";
            double capsuleMs = Folio::Backends::GlCapsuleRenderer::BenchmarkCpuIngestionMs(1000, 50);
            std::cout << "=== GPU CAPSULE SDF 1,000-POINT CPU BENCHMARK ===\n"
                      << "1,000-point BuildCapsuleInstances avg CPU time: "
                      << std::fixed << std::setprecision(4) << capsuleMs
                      << " ms (Target < 0.0500 ms: " << (capsuleMs < 0.05 ? "PASS" : "WARN") << ")\n";

            auto& layerManager = Folio::V2::AppBrain::Instance().GetLayerManager();
            Folio::V2::LoadRepro114(layerManager);
            std::cout << "[FolioInspectorV2] Repro114 loaded successfully ("
                      << layerManager.GetLastRawSamples().size() << " samples).\n";
            Folio::V2::InjectTestScribble(layerManager);
            std::cout << "[FolioInspectorV2] Synthetic scribble verified ("
                      << layerManager.GetLastRawSamples().size() << " samples).\n";

            // Verify all Folio brush recipes through BrushComposer
            auto presets = Folio::BrushComposer::CreateFolioPresets();
            std::cout << "[FolioInspectorV2] Validating " << presets.size() << " Cahier Brush Composer recipes...\n";
            std::vector<Folio::CapsuleInstance> testInstances;
            for (const auto& r : presets) {
                testInstances.clear();
                Folio::BrushComposer::BuildRecipeInstances(
                    layerManager.GetLastFinishedPoints(),
                    Folio::ColorRgba(0, 115, 255, 255), 10.0f,
                    r, 1.25, testInstances
                );
                std::cout << "  - Preset [" << r.name << "]: "
                          << r.coats.size() << " coats -> "
                          << testInstances.size() << " GPU CapsuleInstances generated.\n";
            }
            std::cout << "[FolioInspectorV2] All headless diagnostics PASSED.\n";
            return 0;
        }
    }

#if defined(__ANDROID__)
    // On Android, SDL3 is the main C++ application driver, while FolioInkView (GLFrontBufferedRenderer)
    // is the sole hardware surface. Do NOT initialize SDL_INIT_VIDEO or create a second SDL window/GLContext
    // (Law 1 & Law 1.1: Single Hardware Surface + Single Master Pipeline).
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        std::cerr << "[FolioInspectorV2] SDL_Init(SDL_INIT_EVENTS) failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    auto& brain = Folio::V2::AppBrain::Instance();
    auto& layerManager = brain.GetLayerManager();

    layerManager.onDocumentDirty = [&brain]() {
        brain.RequestRedraw();
    };

    // Request initial front-buffer render pass once surface is ready
    Folio::Backends::RequestAndroidFrontBufferFlush();

    bool running = true;
    while (running) {
        SDL_Event event;
        const bool hasDelayed = (layerManager.GetInspectorGui().simulatedLatencyMs > 0.0f);
        bool hasEvent = false;
        const bool animating = layerManager.HasActiveAnimations();
        if (animating) {
            hasEvent = SDL_WaitEventTimeout(&event, 8);
        } else if (hasDelayed) {
            hasEvent = SDL_WaitEventTimeout(&event, 4);
        } else {
            hasEvent = SDL_WaitEvent(&event);
        }

        while (hasEvent) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_TERMINATING) {
                running = false;
                break;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK) {
                    running = false;
                } else if (event.key.key == SDLK_C) {
                    layerManager.ClearDocument();
                    brain.RequestRedraw();
                }
            }
            hasEvent = SDL_PollEvent(&event);
        }

        if (layerManager.FlushDelayedPackets() || animating) {
            brain.RequestRedraw();
        }

        if (brain.CheckAndResetRedraw()) {
            Folio::Backends::RequestAndroidFrontBufferFlush();
        }
    }

    SDL_Quit();
    return 0;
#else
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "[FolioInspectorV2] SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_Window* window = SDL_CreateWindow(
        "Folio Inspector V2.0",
        1600, 960,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        std::cerr << "[FolioInspectorV2] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "[FolioInspectorV2] SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!Folio::Debug::InitGlLoader()) {
        std::cerr << "[FolioInspectorV2] Failed to initialize OpenGL loader!" << std::endl;
    }

    Folio::Backends::DesktopPresenter presenter;
    presenter.Init(window);

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    if (io.BackendRendererUserData == nullptr) {
        ImGui_ImplOpenGL3_Init("#version 330 core");
    }

    auto& layerManager = Folio::V2::AppBrain::Instance().GetLayerManager();
    bool needsRedraw = true;

    layerManager.onDocumentDirty = [&needsRedraw]() {
        needsRedraw = true;
    };

    int fbWidth = 0, fbHeight = 0;
    SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);

    // Initial render
    glViewport(0, 0, fbWidth, fbHeight);
    layerManager.RenderDocumentCanvas(fbWidth, fbHeight);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    layerManager.RenderImGui(fbWidth, fbHeight, nullptr, /*isSdlHost=*/true);
    SDL_GL_SwapWindow(window);

    GLuint docFbo = 0;
    GLuint docTex = 0;
    int docW = 0;
    int docH = 0;
    bool docFboValid = false;

    auto ensureDocFbo = [&](int w, int h) {
        if (w <= 0 || h <= 0) return;
        if (docFbo != 0 && docW == w && docH == h) return;
        if (docTex != 0) {
            glDeleteTextures(1, &docTex);
            docTex = 0;
        }
        if (docFbo != 0) {
            glDeleteFramebuffers(1, &docFbo);
            docFbo = 0;
        }
        glGenFramebuffers(1, &docFbo);
        glGenTextures(1, &docTex);
        glBindTexture(GL_TEXTURE_2D, docTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, docFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, docTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        docW = w;
        docH = h;
        docFboValid = false;
    };

    auto lastDocRenderTime = std::chrono::steady_clock::now();
    bool wasLiveInking = false;
    uint64_t newestInputNs = 0;
    bool hasNewMotionInput = false;

    // Event Loop
    bool running = true;
    while (running) {
        SDL_Event event;
        bool hasEvent = SDL_PollEvent(&event);
        const bool hasDelayed = (layerManager.GetInspectorGui().simulatedLatencyMs > 0.0f);
        const bool animating = layerManager.HasActiveAnimations();
        if (!hasEvent && !layerManager.IsLiveInkingActive() && !needsRedraw && !hasDelayed && !animating) {
            hasEvent = SDL_WaitEvent(&event);
        }

        bool receivedInputThisFrame = false;
        if (hasEvent) {
            do {
                if (event.type == SDL_EVENT_MOUSE_MOTION ||
                    event.type == SDL_EVENT_PEN_MOTION) {
                    newestInputNs = event.common.timestamp;
                    hasNewMotionInput = true;
                    receivedInputThisFrame = true;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                           event.type == SDL_EVENT_PEN_DOWN ||
                           event.type == SDL_EVENT_PEN_UP) {
                    receivedInputThisFrame = true;
                }
                ProcessSdlEvent(event, window, layerManager, io, needsRedraw, running);
            } while (SDL_PollEvent(&event));
        }

        // Flush any delayed packets scheduled for delivery
        const bool flushedDelayed = layerManager.FlushDelayedPackets();
        if (flushedDelayed) {
            needsRedraw = true;
        }

        const auto frameStart = std::chrono::steady_clock::now();
        const double elapsedDocMs = std::chrono::duration<double, std::milli>(frameStart - lastDocRenderTime).count();
        const bool isLiveInking = layerManager.IsLiveInkingActive();
        const bool shouldRenderFrame = !isLiveInking || receivedInputThisFrame || needsRedraw || (animating && elapsedDocMs >= 16.6);
        if (!shouldRenderFrame) {
            continue;
        }
        SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
        ensureDocFbo(fbWidth, fbHeight);

        if (isLiveInking != wasLiveInking) {
            SDL_GL_SetSwapInterval(isLiveInking ? 0 : 1);
            wasLiveInking = isLiveInking;
        }

        const bool hasActiveVideo = animating;
        const bool docDirty = needsRedraw;
        const bool uiDirty = needsRedraw;
        const bool shouldUpdateDocFbo =
            !docFboValid ||
            docW != fbWidth ||
            docH != fbHeight ||
            ((docDirty || uiDirty || hasActiveVideo) && (!isLiveInking || elapsedDocMs >= 16.6));

        if (shouldUpdateDocFbo && docFbo != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, docFbo);
            glViewport(0, 0, fbWidth, fbHeight);
            glDisable(GL_SCISSOR_TEST);

            layerManager.RenderDocumentCanvas(fbWidth, fbHeight);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            layerManager.RenderImGui(fbWidth, fbHeight, nullptr, /*isSdlHost=*/true);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            lastDocRenderTime = frameStart;
            docFboValid = true;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbWidth, fbHeight);
        glDisable(GL_SCISSOR_TEST);

        if (docFbo != 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, docFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        if (isLiveInking) {
            layerManager.RenderLiveInking(fbWidth, fbHeight);
        }

        const auto beforeSwap = std::chrono::steady_clock::now();
        SDL_GL_SwapWindow(window);
        const auto afterSwap = std::chrono::steady_clock::now();

        auto& telem = layerManager.GetMutableStylusTelemetry();
        telem.engineDrawMs = std::chrono::duration<float, std::milli>(beforeSwap - frameStart).count();
        telem.swapWaitMs = std::chrono::duration<float, std::milli>(afterSwap - beforeSwap).count();
        if (hasNewMotionInput) {
            telem.inputToSwapMs = static_cast<float>(SDL_GetTicksNS() - newestInputNs) / 1e6f;
            hasNewMotionInput = false;
        }
        needsRedraw = false;
    }

    if (docTex != 0) {
        glDeleteTextures(1, &docTex);
    }
    if (docFbo != 0) {
        glDeleteFramebuffers(1, &docFbo);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
#endif
}
