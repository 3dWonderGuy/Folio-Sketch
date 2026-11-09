# Folio Inking Inspector & Diagnostic Workbench Manual

## Overview

The `folio_ink_inspector` (`inspectorV2.0`) is the dedicated diagnostic testbed, performance measurement workbench, and visual analysis harness for Folio-Sketch. It serves two distinct purposes:
1. **Headless Continuous Integration & Benchmarking:** Fast, non-interactive CLI diagnostics for CI pipelines and regression prevention.
2. **Interactive Laboratory Workbench:** Real-time visual telemetry, step-by-step scrubber debugging, and live algorithmic inspection.

---

## 1. Headless CLI Mode: `--diagnose`

For CI servers and regression test runs, the inspector executes without opening an X11, Wayland, or native window:

```bash
./build/bin/folio_ink_inspector --diagnose
```

### Automated Diagnostic Test Battery

1. **1,000-Point CPU Ingestion Microbenchmark:**
   - Synthesizes 1,000 continuous high-frequency digitizer samples across complex curves.
   - Measures end-to-end CPU time through the 4-stage pipeline (calibration, ODE solver, width evaluator, capsule generation).
   - **Performance Verification Target:** CPU execution time must remain **$< 0.05\text{ ms}$** per 1,000 points.

2. **Corner Slivers & Inverted Triangles Test:**
   - Feeds the math engine 10 acute corner angles ranging from $5^\circ$ to $175^\circ$ at varying pen velocities.
   - Validates that capsule bounds, normal calculations, and joint geometry produce zero degenerate slivers, zero NaN/Inf coordinates, and zero inverted triangles.

3. **Built-in Repro Verification Traces (`repro114.inc`):**
   - Replays deterministic historical hardware traces recorded from anomalous touch digitizers (e.g., stylus lift-off glitching, burst sample clustering).
   - Confirms that the ODE solver and Kalman predictor converge cleanly without numerical explosion.

---

## 2. Interactive Laboratory Workbench

When launched without `--diagnose`, `folio_ink_inspector` opens an ImGui-powered hardware diagnostic console with 5 specialized test tabs:

### Tab 1: Centerline Lab
- **Raw Sensor HUD:** Real-time visualization of raw digitizer samples plotted directly alongside the filtered physical mass trajectory.
- **Kalman Prediction Spine:** Visualizes the predicted stroke tip trajectory, curvature cutoff gates, and confidence ellipsoid horizons.
- **240Hz / 480Hz Burst Generator:** Generates synthetic hardware digitizer packet bursts to stress-test high-speed stylus event handling and jitter rejection.

### Tab 2: Pen & Width Lab
- **Profile Customization:**
  - *Round Nib:* Isotropic uniform radial expansion.
  - *Chisel Calligraphy:* Anisotropic angled contact ellipse responsive to stylus tilt and azimuth angle.
  - *Square Highlighter:* Flat rectangular contact patch with angle alignment.
- **Velocity Thinning Controls:** Interactive response curves for min/max velocity, deceleration clamping, and corner bloat compensation.

### Tab 3: Post-Processing Lab
- **Before vs. After RDP Decimation:** Visualizes discarded collinear points vs. retained chord vertices, with live readout of memory reduction percentage.
- **Gaussian Radius Smoothing Overlays:** Color-coded curvature and radius heatmaps demonstrating the smoothing pass across high-frequency pressure variations.

### Tab 4: Shape Snapping
- **Dwell Detection:** Hold-to-snap time-window triggers.
- **Primitive Geometric Fitting:** Real-time fitting of hand-drawn strokes into perfect geometric primitives:
  - Circles & Ellipses
  - Straight Lines & Multi-segment Polylines
  - Rectangles & Triangles

### Tab 5: Playground
- Open canvas for freehand drawing across all production presets:
  - Standard Pen & Fountain Pen
  - Shading Pencil (with multi-octave paper tooth grain)
  - Translucent Highlighter (with `GL_MAX` union coverage)
  - Dynamic Brushes (Wet Paint, Graffiti, Jelly Wobble, Holiday Lights, Twisted Yarn, Musical Notes)

---

## 3. Time-Travel Scrubber

The inspector features a non-destructive historical timeline recording every input sample, ODE state, and generated capsule:

| Keybinding | Action | Description |
| :--- | :--- | :--- |
| `Space` | **Freeze / Unfreeze** | Pauses live inking simulation and preserves current GPU state |
| `[` | **Step Backward** | Rewinds the simulation by 1 sample or sub-step |
| `]` | **Step Forward** | Advances the simulation by 1 sample or sub-step |
| `Home` / `End` | **Jump to Tip / Tail** | Moves immediately to stroke start or stroke conclusion |
| `Mouse Drag` | **Timeline Scrubber** | Scrubs smoothly through stroke generation history |

In frozen mode, the inspector displays normal vectors, tangent vectors, ODE velocity vectors, and instantaneous bounding scissors for each individual segment.
