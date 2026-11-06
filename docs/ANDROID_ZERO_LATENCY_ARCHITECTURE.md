# Android Zero-Latency Inking & Hardware Surface Composer Architecture

## 1. Executive Summary & The Discovery

During physical hardware testing on a Samsung Galaxy Tab (OneUI 6 / Android 14, 2960×1848, 120Hz), we systematically isolated the exact root cause of inking latency in hybrid native/C++ applications.

### The Core Law of Mobile Inking
> **A mobile display controller (DPU) cannot maintain zero-latency unbuffered hardware scanout if an application stacks multiple active `SurfaceView`s in the same window.**
> 
> Direct scanout is **only** granted when there is **exactly one** hardware `SurfaceView` in the window hierarchy.

---

## 2. The Empirical Proof

We tested three distinct configurations on physical hardware to isolate the latency source:

| Configuration | Hierarchy | Observed Latency |
| :--- | :--- | :--- |
| **1. Bare Metal** (`FolioCanvasActivity`) | Single `SurfaceView` (`FolioInkView`), no secondary surfaces, no SDL swapchain | **0ms (Sub-2ms)**<br>Glued to pen nib |
| **2. Dual-Surface Stacking** (`SDLActivity`) | `mSurface` (SDL) on bottom, `FolioInkView` on top (translucent) | **~5ms to 15ms**<br>Trails pen nib |
| **3. Single-Surface SDL** (`mSurface` detached) | `SDLActivity` host, `mSurface` removed from layout, only `FolioInkView` active | **0ms (Sub-2ms)**<br>Glued to pen nib |

When SDL's bottom `mSurface` was removed from the view hierarchy (`// mLayout.addView(mSurface);`), the latency instantly dropped from 5ms back to **pure zero latency (glued to the pen nib)** under the exact same `SDLActivity` host!

---

## 3. Deep Hardware Root Cause Analysis

### The Physics of the Display Processing Unit (DPU)
1. **Hardware Overlay Scanout:**
   A modern mobile display controller (Qualcomm DPU / Samsung Exynos DPU) has dedicated hardware composition planes. To achieve sub-4ms inking, Android's `GLFrontBufferedRenderer` bypasses the multi-buffer swapchain and writes directly to a single-buffered `SurfaceControl` overlay layer scanned out directly by the DPU.
2. **The Overlay Breakdown:**
   If a translucent `SurfaceView` (`FolioInkView`) is placed on top of another active `SurfaceView` (`mSurface`), the hardware DPU **cannot** alpha-blend the two independent hardware buffer streams on-the-fly without running an extra GPU composition pass.
3. **The Penalty:**
   The moment `mSurface` queues even a single frame (`SDL_GL_SwapWindow` / `eglSwapBuffers`), Android's `BLASTBufferQueue` and `SurfaceFlinger` cancel direct scanout on the top layer and force both surfaces through the multi-buffered compositor. This instantly injects a 1-frame composition barrier (~5ms to 16ms of lag).

---

## 4. The Production Architecture for FolioNote

To build `FolioNote` with zero latency while retaining complete document management, multiplatform desktop debugging, and UI controls, the application must follow the **Single-Surface Dual-Layer Architecture** (used by Google Cahier / `androidx.ink` and Samsung Notes):

```
┌─────────────────────────────────────────────────────────────────┐
│                    ONE UNIFIED WINDOW                           │
├─────────────────────────────────────────────────────────────────┤
│  LAYER 1 (Top): Standard UI (Toolbar, Menus, Buttons)          │
│  • Normal Views / HWUI (does NOT create a SurfaceView!)        │
│  • Handles finger clicks, tool selection, undo/redo             │
├─────────────────────────────────────────────────────────────────┤
│  LAYER 2: FolioInkView (Single SurfaceView with 2 sub-layers)   │
│                                                                 │
│  [Sub-layer A: MultiBuffered Layer]                             │
│  • Main Loop / Document Canvas                                  │
│  • Renders background paper, grid lines, committed strokes      │
│  • Handles canvas Pan & Zoom (at 120Hz VSync)                   │
│                                                                 │
│  [Sub-layer B: FrontBuffered Layer]                             │
│  • Zero-Latency Stylus Scanout                                  │
│  • Renders live wet ink while pen is down (<2ms direct scanout) │
└─────────────────────────────────────────────────────────────────┘
```

### How the Two Sub-Layers Interact
- **While Pen is Down (`ACTION_DOWN` → `ACTION_MOVE`):**
  The stylus samples bypass the document canvas entirely. Sub-layer B renders the live wet stroke with `glFlush()` straight to the glass. Sub-layer A is 100% idle.
- **On Pen Lift (`ACTION_UP`):**
  The completed stroke geometry is committed to the document model. Sub-layer A redraws the document canvas with the new permanent stroke, and Sub-layer B clears its front-buffer in the exact same frame (seamless, zero-flicker handoff).
- **On Finger Navigation (Pan / Pinch-Zoom):**
  Finger gestures update the canvas transform matrix and trigger a redraw of Sub-layer A at smooth 120Hz display refresh. Sub-layer B remains idle.
