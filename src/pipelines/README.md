# FolioSketch Output Pipelines (`src/pipelines`)

After `src/engine` smooths raw stylus telemetry and evaluates dynamic stroke radii, the resulting `StrokePoint` stream can be fed into any of Folio-Sketch's three specialized output pipelines:

```
Modeled StrokePoint Stream (x, y, radius, pressure, tilt)
                        │
       ┌────────────────┼────────────────┐
       ▼                ▼                ▼
  [ capsule ]      [ vector ]       [ raster ]
```

---

### 1. [`capsule/`](./capsule/) — Real-Time GPU SDF Capsule Pipeline
* **What it produces:** `std::vector<CapsuleInstance>` & `GpuShapeInstance` (32-byte instanced PODs).
* **How it works:** Emits one analytical capsule per segment $(P_A, P_B, r_A, r_B)$. The GPU fragment shader evaluates the exact Signed Distance Field (SDF) with sub-pixel antialiasing.
* **Best for:** Ultra-low-latency live inking (< 0.05 ms), note-taking, drafting, and infinite-zoom canvases.

---

### 2. [`vector/`](./vector/) — 2D Vector Outline & Export Pipeline
* **What it produces:** `InkPath` (watertight 2D polygon contours, patterns, and hatching infills).
* **How it works:** Sweeps analytical nib profiles (Round, Chisel, Square) along the stroke centerline with miter/bevel/arc corner resolution.
* **Best for:** Resolution-independent SVG, PDF, ThorVG, and Blend2D vector export.

---

### 3. [`raster/`](./raster/) — Procreate-Style Dab & Smudge Pipeline
* **What it produces:** `std::vector<RasterDabInstance>` (32-byte instanced stamp descriptors).
* **How it works:** Walks the stroke curve by arc length ($\Delta s = \text{spacing} \times \text{diameter}$) with continuous cross-frame carry-over, emitting oriented stamps with scatter, paper grain modulation, and wet canvas smudge pickup weights.
* **Best for:** Digital painting, chalk/charcoal/airbrush textures, and wet pixel smudging on FBO canvas layers.
