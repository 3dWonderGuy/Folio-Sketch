# FolioSketch Core Module

This directory contains the foundational, zero-dependency data structures, configurations, and core mathematical algorithms that power the inking pipeline in `FolioSketch`.

---

### 1. `ink_stroke_data.hpp`
* **What it does:** Defines fundamental value types, coordinates, and data structures used everywhere across the engine.
* **Key components:**
  - `Point2D`, `ColorRgba`: 2D point and color primitives.
  - `StylusInputPoint`: Raw hardware sensor packet (position, pressure, time, velocity, tilt).
  - `StrokePoint`: Processed centerline vertex (position, dynamic radius, speed, tangent direction).
  - `Segment1D`: Linear segment between two points with radius bounds and precomputed lengths.

---

### 2. `ink_config.hpp`
* **What it does:** Provides the global runtime configuration and tuning parameters for stroke behavior.
* **Key domains:**
  - `Calibration`: Stylus pressure curve mapping (gamma exponent, deadzones, saturation).
  - `Physics`: Spring-mass-damper ODE parameters for Google Ink stroke smoothing.
  - `Filters`: Micro-tremor hand wobble suppression and cursive loop contraction mitigation.
  - `Prediction`: Forward Kalman latency predictor to reduce glass-to-display latency.
  - `Dynamics`: Dynamic stroke width adjustments driven by pressure, velocity, and pen tilt.

---

### 3. `stroke_collision.hpp`
* **What it does:** High-performance computational geometry for stroke hit-testing and precision erasing.
* **Key components:**
  - `StrokeCollisionEngine`: Point/tap detection, static circle queries, and continuous swept-capsule distance tests (Dan Sunday's segment-to-segment algorithm) to prevent tunneling on fast pen movements.
  - `StrokeSlicer`: Analytic quadratic circle-segment intersection solver that cleanly splits stroke segments into surviving sub-strokes when intersected by an eraser.
