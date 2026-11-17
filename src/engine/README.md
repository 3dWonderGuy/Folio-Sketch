# FolioSketch Engine Module

The engine module is the physical modeling, orchestration, width dynamics, and procedural brush rendering core of Folio-Sketch.

---

### Directory Architecture

```
src/engine/
├── ink_engine.hpp / .cpp           <-- Primary facade & state orchestrator
├── stylus_dynamics.hpp             <-- Calibrated hardware stylus signals (pressure, velocity, tilt, slope)
├── physics/                        <-- Spring-mass ODE modeling & trailing relaxation
│   ├── ink_physics_bridge.hpp / .cpp
│   └── ink_stroke_finisher.hpp / .cpp
├── brush/                          <-- Multi-coat procedural brushes & signal modulators
│   ├── brush_recipe.hpp
│   ├── brush_composer.hpp / .cpp
│   └── brush_presets.cpp
└── post_processing/                <-- Post-stroke optimization, decimation & shape snapping
    ├── stroke_post_processor.hpp
    └── shape_recognizer.hpp
```

---

### 1. Root Facade (`ink_engine.hpp / .cpp`)
* **Role:** The main public entry point and orchestrator for active stroke gestures.
* **Key Components:**
  - Manages the lifecycle of a stroke (`BeginStroke`, `AppendPoint`, `FinishStroke`, `CancelStroke`).
  - Coordinates real-time physics smoothing, Kalman prediction, and calibrated signal evaluation.
  - `FinishStroke()` returns the modeled centerline and collision segments (`FinishedStrokeData`), while rendering outputs (`BuildStrokeCapsules`, `BuildStrokeOutline`, `BuildStrokeRasterDabs`) are generated on demand.

---

### 2. Stylus Dynamics (`stylus_dynamics.hpp`)
* **`StylusDynamics`**: Target-agnostic hardware telemetry calibrator:
  - `EvaluatePressure`: Deadzone, saturation, and power-law gamma curve $\to [0.0, 1.0]$.
  - `FilterVelocity`: Inertial acceleration/deceleration slew-rate clamper eliminating $\Delta t$ jitter spikes.
  - `EvaluateTilt`: Extracts normalized tilt magnitude and barrel azimuth (`StylusTiltSignal`).
  - `ClampSpatialSlope`: Geometric $|dr/ds| \le \text{maxSlope}$ cone-slope limiter.

---

### 3. Physics Subsystem (`physics/`)
* **`ink_physics_bridge.hpp / .cpp`**: Adapts raw hardware `StylusInputPoint` packets into Google's `ink::stroke_model::StrokeModeler` spring-mass-damper ODE simulator.
* **`ink_stroke_finisher.hpp / .cpp`**: Flushes trailing spring-mass iterations upon stylus lift-off to guarantee smooth tail settling and single-tap edge-case resolution.

---

### 4. Brush Subsystem (`brush/`)
* **`brush_recipe.hpp` / `brush_presets.cpp` / `brush_composer.hpp / .cpp`**: Universal multi-coat procedural brush composer inspired by Google Ink Cahier / BrushFamily:
  - Built-in presets (Ballpoint, Marker, Fountain Pen, Highlighter, Pencil, Wet Paint, Graffiti, Holiday Lights, Twisted Yarn, Music Staff, Custom).
  - Procedural signal modulators mapping physical inputs (pressure, tilt, velocity, arc length, age) through waveforms to geometric/color targets.
  - Compiles directly into GPU-instanced `CapsuleInstance` streams for frontbuffer scanout.

---

### 5. Post-Processing Subsystem (`post_processing/`)
* **`stroke_post_processor.hpp`**:
  - **Submerged Disk Culling**: Prunes occluded intermediate disks swallowed by overlapping neighbors.
  - **RDP Curve Simplification**: Shape-preserving Ramer-Douglas-Peucker decimation for minimal memory footprint and fast vector export.
* **`shape_recognizer.hpp`**:
  - **QuickShape Recognition**: Real-time shape classification and primitive fitting ("draw-and-hold to snap" for straight lines, circles, ellipses, triangles, rectangles, and squares).
