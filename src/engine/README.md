# FolioSketch Engine Module (`src/engine`)

This directory contains the internal implementation of the inking engine pipeline. It transforms raw digitizer/stylus inputs (coordinates, timestamp, pressure, tilt) into smoothed, variable-width inking strokes.

---

### Pipeline Architecture

When drawing, input samples pass through four sequential stages coordinated by `InkEngine`:

```
[Raw Stylus Touch] (x, y, pressure, dt)
         │
         ▼
 1. InkPhysicsBridge      (Google Ink Stroke Modeler spring-mass filter & Kalman prediction)
         │
         ▼
 2. StrokeWidthEvaluator  (Pressure gamma, velocity thinning, corner compensation -> assigns radius 'r')
         │
         ▼
 3. InkStrokeFinisher     (On pen lift: tap-dot synthesis, tail tapering, collision capsules)
         │
         ▼
 4. StrokePostProcessor   (RDP curve simplification & Gaussian smoothing)
         │
         ▼
[FinishedStrokeData]     (Centerline + Capsule Instances + Vector Outline + Collision Data)
```

---

### File Inventory & Responsibilities

| File | Role | Description |
| :--- | :--- | :--- |
| `ink_engine.cpp` | **Orchestrator** | Implements the public facade defined in `include/folio_sketch/engine/ink_engine.hpp` (`BeginStroke`, `AppendPoint`, `FinishStroke`, `CancelStroke`). Manages scratch buffers and calls each pipeline stage. |
| `ink_physics_bridge.hpp` / `.cpp` | **Physics Solver** | Private bridge to Google's `ink-stroke-modeler`. Applies spring-mass-damper physics, wobble suppression, and Kalman latency prediction. |
| `ink_stroke_finisher.hpp` / `.cpp` | **Finalization Stage** | Handles stroke completion edge cases: synthesizes dots for stationary taps, applies tail-tapering on flicks, and computes 1D collision capsules for hit-testing/eraser tools. |

---

### Architecture & Design Notes

1. **Private Dependency Insulation (PIMPL):**
   `ink_physics_bridge.hpp` is intentionally kept inside `src/engine/` rather than the public `include/` directory. Google's `ink-stroke-modeler` depends on Abseil headers; keeping this bridge private ensures consumer applications linking against `FolioSketch` do not need Google Modeler or Abseil headers in their search paths.

2. **Accompanying Public Math Modules (`include/folio_sketch/engine/`):**
   - `stroke_width_evaluator.hpp`: Dynamic line thickness evaluation (header-only).
   - `stroke_post_processor.hpp`: Ramer-Douglas-Peucker (RDP) vertex decimation and Gaussian radius smoothing (header-only).
