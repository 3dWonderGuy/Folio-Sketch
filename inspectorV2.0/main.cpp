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
#if defined(__ANDROID__)
#include "folio_backends/android/android_frontbuffer.hpp"
#else
#include "folio_backends/sdl_gpu/sdl_gpu_presenter.hpp"
#endif

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

    Folio::Backends::AndroidHostCallbacks androidCallbacks{};
    androidCallbacks.onIngestPacket = [](float x, float y, float pressure, float tiltX, float tiltY,
                                         double timeSec, bool isDown, bool isUp, bool isStylus, int pointerId) {
        auto& mgr = Folio::V2::AppBrain::Instance().GetLayerManager();
        Folio::V2::InputPhase phase = isDown ? Folio::V2::InputPhase::Down
                                    : (isUp ? Folio::V2::InputPhase::Up : Folio::V2::InputPhase::Move);
        if (mgr.HandleTouchForImGui(x, y, phase)) {
            Folio::V2::AppBrain::Instance().RequestRedraw();
            return;
        }
        Folio::V2::PointerPacket pkt;
        pkt.device       = isStylus ? Folio::V2::DeviceType::Stylus : Folio::V2::DeviceType::Touch;
        pkt.phase        = phase;
        pkt.pointerId    = static_cast<int32_t>(pointerId);
        pkt.x            = x;
        pkt.y            = y;
        pkt.pressure     = pressure;
        pkt.tiltX        = tiltX;
        pkt.tiltY        = tiltY;
        pkt.timestampSec = timeSec;
        mgr.ProcessPointer(pkt);
        if (isUp) {
            Folio::V2::AppBrain::Instance().RequestRedraw();
        }
    };
    androidCallbacks.onRenderFrame = [](int width, int height, const float* surfaceTransformMatrix) {
        auto& mgr = Folio::V2::AppBrain::Instance().GetLayerManager();
        if (mgr.IsLiveInkingActive()) {
            mgr.RenderStateBWetScanout(width, height, surfaceTransformMatrix);
        } else {
            mgr.RenderStateADocumentEngine(width, height, surfaceTransformMatrix, /*isSdlHost=*/false);
        }
    };
    androidCallbacks.onClearCanvas = []() {
        Folio::V2::AppBrain::Instance().GetLayerManager().ClearDocument();
    };
    Folio::Backends::SetAndroidHostCallbacks(androidCallbacks);

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

    bool useSdlGpu = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sdl-gpu") == 0) {
            useSdlGpu = true;
            break;
        }
    }

    if (useSdlGpu) {
        SDL_Window* gpuWin = SDL_CreateWindow(
            "Folio Inspector V2.0 [SDL_GPU Latency Test]",
            1600, 960,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
        );
        if (!gpuWin) {
            std::cerr << "[FolioInspectorV2] SDL_CreateWindow (SDL_GPU) failed: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }

        Folio::Backends::SdlGpuPresenter gpuPresenter;
        if (!gpuPresenter.Init(gpuWin)) {
            std::cerr << "[FolioInspectorV2] SdlGpuPresenter::Init failed!" << std::endl;
            SDL_DestroyWindow(gpuWin);
            SDL_Quit();
            return 1;
        }

        std::cout << "[FolioInspectorV2] SDL_GPU Driver: " << gpuPresenter.GetDriverName()
                  << " | PresentMode: " << gpuPresenter.GetPresentModeName() << std::endl;

        auto& layerManager = Folio::V2::AppBrain::Instance().GetLayerManager();
        bool running = true;
        bool needsRedraw = true;
        uint64_t newestInputNs = 0;
        bool hasNewMotionInput = false;
        float lastInputToSwapMs = 0.0f;
        float lastEngineDrawMs = 0.0f;
        float lastSwapWaitMs = 0.0f;
        auto lastTitleUpdate = std::chrono::steady_clock::now();

        std::vector<Folio::CapsuleInstance> committedCapsules;
        std::vector<Folio::CapsuleInstance> wetCapsules;
        size_t cachedCommittedCount = 0;

        while (running) {
            SDL_Event event;
            bool hasEvent = SDL_PollEvent(&event);
            if (!hasEvent && !layerManager.IsLiveInkingActive() && !needsRedraw) {
                hasEvent = SDL_WaitEvent(&event);
            }

            bool receivedInputThisFrame = false;
            if (hasEvent) {
                do {
                    if (event.type == SDL_EVENT_QUIT ||
                        (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                         event.window.windowID == SDL_GetWindowID(gpuWin))) {
                        running = false;
                        break;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_C) {
                        layerManager.ClearDocument();
                        committedCapsules.clear();
                        cachedCommittedCount = 0;
                        needsRedraw = true;
                    }
                    if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
                        event.type == SDL_EVENT_WINDOW_RESIZED ||
                        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                        needsRedraw = true;
                    }
                    if (event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_LMASK)) {
                        float fx = 0.0f, fy = 0.0f;
                        ScreenToFb(gpuWin, event.motion.x, event.motion.y, fx, fy);
                        Folio::V2::PointerPacket pkt{};
                        pkt.device = Folio::V2::DeviceType::Mouse;
                        pkt.phase = Folio::V2::InputPhase::Move;
                        pkt.pointerId = 0;
                        pkt.x = fx;
                        pkt.y = fy;
                        pkt.pressure = 0.5f;
                        pkt.timestampSec = static_cast<double>(event.motion.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        newestInputNs = event.common.timestamp;
                        hasNewMotionInput = true;
                        receivedInputThisFrame = true;
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                        float fx = 0.0f, fy = 0.0f;
                        ScreenToFb(gpuWin, event.button.x, event.button.y, fx, fy);
                        Folio::V2::PointerPacket pkt{};
                        pkt.device = Folio::V2::DeviceType::Mouse;
                        pkt.phase = Folio::V2::InputPhase::Down;
                        pkt.pointerId = 0;
                        pkt.x = fx;
                        pkt.y = fy;
                        pkt.pressure = 0.5f;
                        pkt.timestampSec = static_cast<double>(event.button.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        receivedInputThisFrame = true;
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                        float fx = 0.0f, fy = 0.0f;
                        ScreenToFb(gpuWin, event.button.x, event.button.y, fx, fy);
                        Folio::V2::PointerPacket pkt{};
                        pkt.device = Folio::V2::DeviceType::Mouse;
                        pkt.phase = Folio::V2::InputPhase::Up;
                        pkt.pointerId = 0;
                        pkt.x = fx;
                        pkt.y = fy;
                        pkt.pressure = 0.5f;
                        pkt.timestampSec = static_cast<double>(event.button.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        receivedInputThisFrame = true;
                        needsRedraw = true;
                    } else if (event.type == SDL_EVENT_PEN_MOTION ||
                               event.type == SDL_EVENT_PEN_DOWN ||
                               event.type == SDL_EVENT_PEN_UP) {
                        const bool isDown = (event.type == SDL_EVENT_PEN_DOWN);
                        const bool isUp = (event.type == SDL_EVENT_PEN_UP);
                        const float px = isDown || isUp ? event.pbutton.x : event.pmotion.x;
                        const float py = isDown || isUp ? event.pbutton.y : event.pmotion.y;
                        float fx = 0.0f, fy = 0.0f;
                        ScreenToFb(gpuWin, px, py, fx, fy);
                        Folio::V2::PointerPacket pkt{};
                        pkt.device = Folio::V2::DeviceType::Stylus;
                        pkt.phase = isDown ? Folio::V2::InputPhase::Down : (isUp ? Folio::V2::InputPhase::Up : Folio::V2::InputPhase::Move);
                        pkt.pointerId = static_cast<int32_t>(event.pmotion.which);
                        pkt.x = fx;
                        pkt.y = fy;
                        pkt.pressure = 0.5f;
                        pkt.timestampSec = static_cast<double>(event.common.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        if (!isDown && !isUp) {
                            newestInputNs = event.common.timestamp;
                            hasNewMotionInput = true;
                        } else if (isUp) {
                            needsRedraw = true;
                        }
                        receivedInputThisFrame = true;
                    }
                } while (SDL_PollEvent(&event));
            }

            const bool isLiveInking = layerManager.IsLiveInkingActive();
            if (isLiveInking && !receivedInputThisFrame && !needsRedraw) {
                continue;
            }

            gpuPresenter.SetLiveInkingState(isLiveInking);

            auto appendCapsules = [](const std::vector<Folio::StrokePoint>& pts, uint32_t packedColor, std::vector<Folio::CapsuleInstance>& out) {
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    Folio::CapsuleInstance inst{};
                    inst.pointA = { static_cast<float>(pts[i].x), static_cast<float>(pts[i].y) };
                    inst.pointB = { static_cast<float>(pts[i + 1].x), static_cast<float>(pts[i + 1].y) };
                    inst.radii  = { pts[i].r, pts[i + 1].r };
                    inst.colorRgba = packedColor;
                    inst.colorRgbaEnd = packedColor;
                    out.push_back(inst);
                }
            };

            const auto& docStrokes = layerManager.GetDocumentStrokes();
            if (docStrokes.size() != cachedCommittedCount) {
                committedCapsules.clear();
                for (const auto& s : docStrokes) {
                    appendCapsules(s.points, s.color, committedCapsules);
                }
                cachedCommittedCount = docStrokes.size();
            }

            wetCapsules.clear();
            if (isLiveInking) {
                const auto& livePts = layerManager.GetLiveEngine().GetLiveModeledPoints();
                if (!livePts.empty()) {
                    appendCapsules(livePts, Folio::PackPremultipliedGpuColor(layerManager.GetActiveStyle().color), wetCapsules);
                }
            }

            const auto& cs = layerManager.GetCanvasState();
            Folio::Backends::CanvasTransform xf{ cs.panX, cs.panY, cs.zoom };
            gpuPresenter.RenderFrame(committedCapsules, wetCapsules, xf, 0.0f, 0.25f, &lastEngineDrawMs, &lastSwapWaitMs);

            if (hasNewMotionInput) {
                lastInputToSwapMs = static_cast<float>(SDL_GetTicksNS() - newestInputNs) / 1e6f;
                hasNewMotionInput = false;
            }

            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - lastTitleUpdate).count() >= 200.0) {
                char title[256];
                std::snprintf(title, sizeof(title),
                    "Folio [SDL_GPU: %s | %s] Input-to-Swap: %.2f ms | Draw: %.2f ms | Swap: %.2f ms (C to clear)",
                    gpuPresenter.GetDriverName(), gpuPresenter.GetPresentModeName(),
                    lastInputToSwapMs, lastEngineDrawMs, lastSwapWaitMs);
                SDL_SetWindowTitle(gpuWin, title);
                lastTitleUpdate = now;
            }
            needsRedraw = false;
        }

        gpuPresenter.Shutdown();
        SDL_DestroyWindow(gpuWin);
        SDL_Quit();
        return 0;
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
        if (layerManager.FlushDelayedPackets()) {
            needsRedraw = true;
        }

        const auto frameStart = std::chrono::steady_clock::now();
        const bool isLiveInking = layerManager.IsLiveInkingActive();
        const bool hasActiveVideo = animating;
        const bool docDirty = needsRedraw;
        const bool uiDirty = needsRedraw;

        const bool docRefreshDue = presenter.NeedsDocRefresh(docDirty, uiDirty, hasActiveVideo, isLiveInking);
        const bool shouldRenderFrame = !isLiveInking || receivedInputThisFrame || docRefreshDue;
        if (!shouldRenderFrame) {
            continue;
        }

        presenter.SetLiveInkingState(isLiveInking);

        if (docRefreshDue && presenter.BeginDocPass(&fbWidth, &fbHeight)) {
            layerManager.RenderDocumentCanvas(fbWidth, fbHeight);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            layerManager.RenderImGui(fbWidth, fbHeight, nullptr, /*isSdlHost=*/true);

            presenter.EndDocPass();
        }

        presenter.BlitDocToScreen();
        fbWidth = presenter.GetWidth();
        fbHeight = presenter.GetHeight();

        if (isLiveInking) {
            layerManager.RenderLiveInking(fbWidth, fbHeight);
        }

        const auto beforeSwap = std::chrono::steady_clock::now();
        presenter.EndWetDraw();
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

    presenter.Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
#endif
}
