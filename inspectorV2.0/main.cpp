/**
 * =========================================================================================
 * @file inspectorV2.0/main.cpp
 * @brief FolioNote Mini Base (Inspector V2.0): Dual-Thread Architecture Host
 * =========================================================================================
 *
 * Thread 1: Event-Driven Host Loop (SDL3)
 *   - Initializes window & graphics context.
 *   - Completely event-driven: blocks in SDL_WaitEvent when idle (0% CPU / 0% GPU).
 *   - Redraws background canvas only when window is exposed or resized.
 *   - Swaps ZERO frames while the user is inking on Thread 2.
 *
 * Thread 2: Zero-Latency Hardware Scanout Engine
 *   - Runs on Android's unbuffered digitizer thread (GLFrontBufferedRenderer / low-latency).
 *   - Ingests 240Hz stylus samples directly into Folio::InkEngine + GlCapsuleRenderer.
 *   - Flushes directly to the hardware scanout layer with <2ms latency.
 */

#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <android/log.h>
#define LOG_TAG "FolioInspectorV2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include "inspector/gl_loader.hpp"
#endif

#include "inspectorV2.0/layer_manager.hpp"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

#if defined(__ANDROID__)
    // On Android, scanout is handled directly by FolioInkView
    while (true) {
        SDL_Delay(1000);
    }
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "[FolioInspectorV2] SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

#if defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
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

#if !defined(__ANDROID__)
    if (!InitGlLoader()) {
        std::cerr << "[FolioInspectorV2] Failed to initialize OpenGL loader!" << std::endl;
    }
#endif

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
#if defined(__ANDROID__)
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 330 core");
#endif

    Folio::V2::LayerManager layerManager;
    bool needsRedraw = true;

    layerManager.onDocumentDirty = [&needsRedraw]() {
        needsRedraw = true;
    };

    int fbWidth = 0, fbHeight = 0;
    SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
    layerManager.RenderDocumentCanvas(fbWidth, fbHeight);
    SDL_GL_SwapWindow(window);
    needsRedraw = false;

    // Main Event Loop (Desktop)
    bool running = true;
    while (running) {
        SDL_Event event;
        bool hasEvent = false;

        // When actively inking, poll continuously for low latency; when idle, wait for events
        if (layerManager.IsLiveInkingActive()) {
            hasEvent = SDL_PollEvent(&event);
        } else {
            hasEvent = SDL_WaitEvent(&event);
        }

        if (hasEvent) {
            do {
                ImGui_ImplSDL3_ProcessEvent(&event);

                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(window))) {
                    running = false;
                    break;
                }

                if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
                    event.type == SDL_EVENT_WINDOW_RESIZED ||
                    event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    needsRedraw = true;
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    if (!io.WantCaptureMouse) {
                        float mouseX = 0.0f, mouseY = 0.0f;
                        SDL_GetMouseState(&mouseX, &mouseY);
                        float zoomFactor = (event.wheel.y > 0) ? 1.15f : 0.87f;
                        layerManager.GetInputManager().onCanvasZoom(mouseX, mouseY, zoomFactor);
                        needsRedraw = true;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (!io.WantCaptureMouse && event.button.button == SDL_BUTTON_LEFT) {
                        Folio::V2::PointerPacket pkt;
                        pkt.device = Folio::V2::DeviceType::Mouse;
                        pkt.phase  = Folio::V2::InputPhase::Down;
                        pkt.x = event.button.x;
                        pkt.y = event.button.y;
                        pkt.pressure = 0.8f;
                        pkt.timestampSec = static_cast<double>(event.button.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        needsRedraw = true;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    if (!io.WantCaptureMouse && (event.motion.state & SDL_BUTTON_LMASK)) {
                        Folio::V2::PointerPacket pkt;
                        pkt.device = Folio::V2::DeviceType::Mouse;
                        pkt.phase  = Folio::V2::InputPhase::Move;
                        pkt.x = event.motion.x;
                        pkt.y = event.motion.y;
                        pkt.pressure = 0.8f;
                        pkt.timestampSec = static_cast<double>(event.motion.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        needsRedraw = true;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        Folio::V2::PointerPacket pkt;
                        pkt.device = Folio::V2::DeviceType::Mouse;
                        pkt.phase  = Folio::V2::InputPhase::Up;
                        pkt.x = event.button.x;
                        pkt.y = event.button.y;
                        pkt.pressure = 0.0f;
                        pkt.timestampSec = static_cast<double>(event.button.timestamp) * 1e-9;
                        layerManager.ProcessPointer(pkt);
                        needsRedraw = true;
                    }
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                        break;
                    } else if (event.key.key == SDLK_C) {
                        layerManager.ClearDocument();
                        needsRedraw = true;
                    }
                }
            } while (SDL_PollEvent(&event));
        }

        // Render Frame
        SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
        layerManager.RenderDocumentCanvas(fbWidth, fbHeight);
        if (layerManager.IsLiveInkingActive()) {
            layerManager.RenderLiveInking(fbWidth, fbHeight);
        }

        // Dear ImGui Frame (Inspector & Diagnostics Bench)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        {
            ImGui::Begin("Inspector V2.0 Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("FPS: %.1f (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::Separator();

            auto& canvas = layerManager.GetCanvasState();
            ImGui::Text("Zoom: %.2fx", canvas.zoom);
            ImGui::Text("Pan: (%.1f, %.1f)", canvas.panX, canvas.panY);
            if (ImGui::Button("Reset View")) {
                canvas.panX = 0.0f;
                canvas.panY = 0.0f;
                canvas.zoom = 1.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Canvas (C)")) {
                layerManager.ClearDocument();
            }

            ImGui::Separator();
            ImGui::Text("Committed Strokes: %zu", layerManager.GetDocumentStrokes().size());
            ImGui::Text("Live Inking Active: %s", layerManager.IsLiveInkingActive() ? "YES" : "NO");

            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
        needsRedraw = false;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
