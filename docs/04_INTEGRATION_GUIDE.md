# Production Application Integration Guide

## Overview

Integrating `folio_sketch` and `folio_backends` into a host application requires zero dependency on the diagnostic inspector. A clean, production-grade integration relies on a two-layer rendering model:
1. **Wet Layer (Foreground Inking Overlay):** Ultra-low latency, single-pass incremental rendering of the active stroke directly to the front buffer or dedicated overlay.
2. **Dry Layer (Background Document FBO):** Double-buffered, static document canvas (`BakedCanvasLayer`) storing committed historical strokes.

---

## Wet-to-Dry Handoff Lifecycle

The diagram below illustrates the end-to-end lifecycle when a user touches the stylus to the screen and completes a stroke:

```
Stylus Hardware              Host App / Bridge              Folio Math Core            GPU / Display
      |                              |                             |                         |
      |--- OnPointerDown ----------->|                             |                         |
      |    (x, y, p, t, ns)          |--- InkEngine::StartStroke ->|                         |
      |                              |                             |                         |
      |--- OnPointerMotion --------->|                             |                         |
      |    (batch samples)           |--- InkEngine::ProcessSamples|                         |
      |                              |<-- std::span<CapsuleInstance>                         |
      |                              |                             |                         |
      |                              |--- GlCapsuleRenderer::UploadIncremental ------------->|
      |                              |--- GlCapsuleRenderer::DrawInstanced ----------------->|
      |                              |--- glFlush() [Direct Scanout Bypass] ---------------->| [Panel Photon]
      |                              |                             |                         |
      |--- OnPointerUp ------------->|                             |                         |
      |    (lift off)                |--- InkEngine::FinishStroke->|                         |
      |                              |<-- FinishedStrokeData (RDP + Vector InkPath)          |
      |                              |                             |                         |
      |                              |--- BakeToDocumentFBO(FinishedStrokeData) ------------>| [Document FBO]
      |                              |--- GlCapsuleRenderer::Reset() ----------------------->| [Clear Wet Overlay]
      |                              |--- RequestDocumentRedraw() -------------------------->| [Composited View]
      v                              v                             v                         v
```

---

## Coordinating Wet Overlays and Document FBOs

See [02. Backends & Hardware Scanout](02_BACKENDS_AND_SCANOUT.md) for the complete architecture of `DesktopPresenter`, `SdlGpuPresenter`, and `AndroidFrontBuffer`.

### Desktop Host Integration (`DesktopPresenter` — OpenGL 3.3 + SDL3)
Use `Folio::Backends::DesktopPresenter` to get decoupled 60 Hz UI/video rendering inside `m_docFbo` and sub-millisecond fullscreen wet-ink blits without VAO sharing or scissor artifacts:

```cpp
Folio::Backends::DesktopPresenter presenter;
presenter.Init(window); // Single double-buffered SDL_GLContext

// Inside your SDL event loop (only render during live inking if input arrived or 60Hz refresh is due):
const bool isLiveInking = bridge.IsActive();
const bool docRefreshDue = presenter.NeedsDocRefresh(docDirty, uiDirty, hasActiveVideo, isLiveInking);
if (isLiveInking && !receivedInputThisFrame && !docRefreshDue) {
    continue; // Prevent 3-buffer swapchain overflow
}

presenter.SetLiveInkingState(isLiveInking); // SwapInterval(0) while inking, (1) on pen-up

if (docRefreshDue && presenter.BeginDocPass(&fbW, &fbH)) {
    RenderDocumentAndVideoAndUI(fbW, fbH);
    presenter.EndDocPass();
}

presenter.BlitDocToScreen(); // Fullscreen glBlitFramebuffer (scissor disabled)
if (isLiveInking) {
    bridge.RenderWet(fbW, fbH); // Draws directly on FBO 0 in main GL context
}
presenter.EndWetDraw(); // SDL_GL_SwapWindow
```

### Android Host Integration (`AndroidHostCallbacks`)
Register your engine callbacks at native startup so `folio_backends/android/folio_android_jni.cpp` forwards `GLFrontBufferedRenderer` events without depending on `inspectorV2.0/`:

```cpp
Folio::Backends::AndroidHostCallbacks cb{};
cb.onIngestPacket = OnHostPointerPacket;
cb.onRenderFrame  = OnHostRenderFrame;
cb.onClearCanvas  = OnHostClearCanvas;
Folio::Backends::SetAndroidHostCallbacks(cb);
```

---

## Turnkey Host Inking Bridge (`CanvasInkingBridge`)

The following minimal, self-contained C++ class encapsulates the bridge between host windowing events and the Folio engine:

```cpp
#include "engine/ink_engine.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"

class CanvasInkingBridge {
public:
    CanvasInkingBridge(float dpi)
        : m_pxToMm(25.4f / dpi)
    {
        m_renderer.Init();
    }

    void OnPointerDown(float xPx, float yPx, float pressure, uint64_t timestampNs) {
        m_active = true;
        m_uploadedCount = 0;
        m_renderer.Reset();

        Folio::StylusInputPoint sample{
            .x = xPx * m_pxToMm,
            .y = yPx * m_pxToMm,
            .pressure = pressure,
            .timeNs = static_cast<int64_t>(timestampNs)
        };
        m_engine.StartStroke(sample);
    }

    void OnPointerMotion(float xPx, float yPx, float pressure, uint64_t timestampNs) {
        if (!m_active) return;

        Folio::StylusInputPoint sample{
            .x = xPx * m_pxToMm,
            .y = yPx * m_pxToMm,
            .pressure = pressure,
            .timeNs = static_cast<int64_t>(timestampNs)
        };
        auto newCapsules = m_engine.ProcessSample(sample);

        if (!newCapsules.empty()) {
            m_renderer.UploadIncremental(m_uploadedCount, newCapsules);
            m_uploadedCount += newCapsules.size();
            m_renderer.DrawInstanced(m_uploadedCount);
            // On direct scanout backends (e.g. Android front-buffer), call glFlush() immediately:
            // glFlush();
        }
    }

    void OnPointerUp(std::function<void(const Folio::FinishedStrokeData&)> onCommit) {
        if (!m_active) return;
        m_active = false;

        Folio::FinishedStrokeData finished = m_engine.FinishStroke();
        if (onCommit) {
            onCommit(finished); // Bake to background document FBO
        }
        m_renderer.Reset();
    }

private:
    float m_pxToMm{1.0f};
    bool m_active{false};
    size_t m_uploadedCount{0};
    Folio::InkEngine m_engine;
    Folio::Backends::GlCapsuleRenderer m_renderer;
};
```
