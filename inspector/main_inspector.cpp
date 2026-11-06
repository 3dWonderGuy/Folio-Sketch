/**
 * =========================================================================================
 * @file inspector/main_inspector.cpp
 * @brief FolioNote Mini Base: Dual-Thread Architecture Test Bench
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

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <android/log.h>
#define LOG_TAG "FolioMiniHost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include "inspector/gl_loader.hpp"
#endif

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
        std::cerr << "[FolioMiniHost] SDL_Init failed: " << SDL_GetError() << std::endl;
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
        "FolioNote Mini Base",
        1600, 960,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        std::cerr << "[FolioMiniHost] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "[FolioMiniHost] SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#if !defined(__ANDROID__)
    if (!InitGlLoader()) {
        std::cerr << "[FolioMiniHost] Failed to initialize OpenGL loader!" << std::endl;
    }
#endif

    // Initial render: draw off-white paper canvas
    int fbWidth = 0, fbHeight = 0;
    SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.96f, 0.96f, 0.97f, 1.0f); // Clean notebook paper color
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window);

    // Main Event Loop (Thread 1)
    // Purely event-driven: sleeps on SDL_WaitEvent with 0% CPU & 0% GPU.
    // Leaves 100% GPU bandwidth free for Thread 2 (stylus inking scanout).
    bool running = true;
    while (running) {
        SDL_Event event;
        if (!SDL_WaitEvent(&event)) {
            break;
        }

        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(window))) {
            running = false;
            break;
        }

        if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
            event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
            glViewport(0, 0, fbWidth, fbHeight);
            glClearColor(0.96f, 0.96f, 0.97f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
        }
    }

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
