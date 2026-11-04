# FolioSketch Engine Module

This directory contains the core orchestration logic that takes raw stylus events and processes them into smooth, modeled strokes with dynamic thickness and curve decimation.

---

### 1. `ink_engine.hpp`
* **What it does:** The main public entry point and orchestrator for stroke drawing.
* **Key components:**
  - `InkEngine`: Manages the lifecycle of a stroke (`BeginStroke`, `AppendPoint`, `FinishStroke`, `CancelStroke`).
  - Coordinates physics prediction, real-time segment updates, dynamic width calculation, and output generation.
  - Generates both live GPU capsule representations and completed stroke representations (`FinishedStrokeData`).

---

### 2. `stroke_width_evaluator.hpp`
* **What it does:** Calculates the dynamic thickness/width of the stroke at each point based on input dynamics and physical constraints.
* **Key components:**
  - Non-linear hardware pressure curve mapping.
  - Inertial acceleration/deceleration clamping to prevent jerky width changes.
  - Corner turn-rate compensation to eliminate bloating around tight corners.
  - Velocity thinning (fountain pen hydrodynamic effect) and cone-slope radius limits ($dr/ds$).
  - Exponential Moving Average (EMA) filtering for smooth transitions.

---

### 3. `stroke_post_processor.hpp`
* **What it does:** Performs cleanup, optimization, and simplification on stroke geometry after drawing.
* **Key components:**
  - `StrokePostProcessor::Simplify`: Shape-preserving Ramer-Douglas-Peucker (RDP) curve decimation.
  - Prunes redundant collinear vertices while preserving sharp turns, arcs, and thickness profiles.
