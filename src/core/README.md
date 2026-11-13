# FolioSketch Core Module (`include/folio_sketch/core`)

All components live in `namespace Folio`, depend strictly on the C++20 standard library, and maintain complete decoupling from platform windowing APIs, operating system SDKs, and graphics rendering contexts.

---

## Architecture & File Map

```
include/folio_sketch/core/
├── ink_stroke_data.hpp    # Fundamental value types, coordinates, colors, and global StrokeStyle
├── stroke_presets.hpp     # Ready-to-use factory presets (Ballpoint, Marker, Fountain, Highlighter, etc.)
├── ink_config.hpp         # Hierarchical runtime physics, calibration, and filtering configuration
└── stroke_collision.hpp   # Continuous swept-capsule hit-testing and analytic quadratic slicing
```

---

## Detailed File Specifications

### 1. `ink_stroke_data.hpp` — Fundamental Types & Stylus Telemetry
* **Role:** Establishes coordinates, colors, centerline samples, global stroke styles, and geometric primitives used across all downstream modules (`engine`, `gpu`, `geometry`, `vector`).
* **Key Components:**
  - **Coordinates & Colors:**
    - `Point2D`: Double-precision $(x, y)$ coordinate in physical millimeters ($\text{mm}$).
    - `ColorRgba`: 32-bit color representation (`r`, `g`, `b`, `a`) with fast bit-packed serialization (`toU32()` returning `0xAABBGGRR` / integer hex conversions).
  - **Stylus Telemetry & Trajectory Points:**
    - `StylusInputPoint`: Hardware digitizer packet before physics simulation (position in mm, contact pressure $[0.0, 1.0]$, timestamp in seconds / nanoseconds, velocity, tilt projection angles).
    - `StrokePoint`: Processed centerline sample produced by the physics modeler, deriving directly from `StylusInputPoint` and adding dynamic stroke half-thickness radius $r$. Implicitly convertible to `Point2D`.
  - **Global Stroke Aesthetics:**
    - `StrokePattern`: Visual geometric line patterns (`Solid`, `Dashed`, `Dotted`, `DashDot`, `Custom`).
    - `StrokeStyle`: Clean global configuration of a stroke gesture: base thickness (`baseWidth`), pattern (`StrokePattern`), color (`ColorRgba`), and dynamic modulation flags (`usePressure`, `useVelocity`, `useTilt`).
  - **Closed Shape Infill:**
    - `InfillStyle`: Geometric fill rules (`InfillType::None`, `Solid`, `HatchDiagonal`, `HatchCross`, `HatchHorizontal`, `HatchVertical`, `HatchDots`) and hatch colors for closed vector loops.
  - **Collision Primitives:**
    - `Segment1D`: Line segment ($p_0 \to p_1$) parameterized with physical width for spatial collision indexing.

---

### 2. `stroke_presets.hpp` — Turnkey Tool Presets
* **Role:** Developer convenience header providing ready-to-use factory constructors for standard inking tools:
  - `StrokePresets::BallpointPen(width, color)`: Crisp, pressure-modulated isotropic line.
  - `StrokePresets::Marker(width, color)`: Constant-width felt-tip marker.
  - `StrokePresets::FountainPen(width, color)`: Hydrodynamic velocity thinning.
  - `StrokePresets::Highlighter(width, color)`: Translucent highlighting.
  - `StrokePresets::ShadingPencil(width, color)`: Tilt-responsive graphite shading.
  - `StrokePresets::DashedLine(width, color)`, `DottedLine`, `DashDotLine`: Standard drafting lines.

---

### 3. `ink_config.hpp` — Comprehensive Physics & Modeling Configuration
* **Role:** Centralized runtime tuning repository. Governs the entire mathematical behavior of the pipeline across 7 distinct operational domains inside `InkingConfig`:
  1. **`Calibration` (Stylus Normalization):**
     - Deadzone and saturation clamping (`pressureDeadzoneMin`, `pressureSaturationMax`).
     - Power-law gamma exponent (`pressureGamma`): $< 1.0$ produces responsive soft touch; $> 1.0$ requires firmer pressure for fine calligraphic control.
  2. **`Physics` (Google Ink Mass-Spring-Damper ODE Solver):**
     - Spring stiffness ratio ($k/m \approx 0.000339$) and damping drag constant ($\gamma = 160.0$) preventing overshoot oscillations around sharp corners.
     - Min output rate upsampling ($180\text{ Hz}$) to eliminate stepped corners on slow pen travel.
     - Angular curve upsampling (`enableAngleUpsampling`, $> 3.4^\circ$) subdividing tight curves.
     - Pen-up relaxation settling loop (`endOfStrokeStoppingDistanceMm < 0.01\text{ mm}`, capped at 20 iterations) to smoothly converge trailing inertia.
  3. **`Filters` (Tremor Cancellation & Loop Preservation):**
     - Google Ink low-speed hand tremor cancellation (`wobbleSpeedFloor = 1.31\text{ mm/s}` to `wobbleSpeedCeiling = 1.44\text{ mm/s}`).
     - Cursive loop contraction mitigation preserving tight handwritten loops from spring dampening.
  4. **`Prediction` (Forward Kalman Latency Compensation):**
     - Lookahead extrapolation ($2\text{--}10\text{ ms}$) configured with acceleration and jerk weights.
     - Curvature suppression cutoff ($\Delta\theta > 30^\circ$ / $\cos\theta < 0.866$) to eliminate corner overshooting "whiskers".
     - Tip tapering ($30\%$ base radius) and distance-attenuated alpha fading.
  5. **`Dynamics` (Width, Velocity & Spatial Clamping):**
     - Asymmetric acceleration filter ($3500\text{ mm/s}^2$ accel vs $2500\text{ mm/s}^2$ decel) stabilizing velocity thinning transitions.
     - Corner turn-rate bloat compensation blending corner velocity toward cruising speed.
     - Spatial cone-slope clamp ($|dr/ds| \le 0.35\text{ mm/mm}$) preventing radius expansion during low-displacement movements.
     - 2-pass spatial Gaussian radius smoothing ($\sigma = 3.5\text{ mm}$) to smooth high-frequency pressure jitter.
  6. **`PostProcessing` (Stroke Decimation):**
     - Stationary envelopment pruning redundant interior vertices where $\Delta s \le |\Delta r|$.
     - Ramer-Douglas-Peucker (RDP) chord decimation reducing control point count by $70\%\text{--}85\%$ while maintaining visual fidelity.
  7. **`ShapeSnapping` (Geometric Primitive Detection):**
     - Dwell detection ($0.5\text{ s}$ hold window within $1.5\text{ mm}$) triggering automatic recognition and snapping to perfect lines, rectangles, circles, and ellipses.

---

### 4. `stroke_collision.hpp` — Collision Detection & Vector Slicing
* **Role:** High-performance computational geometry engine for stylus hit-testing, selection lasso queries, and vector eraser carving.
* **Core Classes & Algorithms:**
  - **`StrokeCollisionEngine`:**
    - `HitTestPoint()`: Fast distance-squared test for single-point dot taps (periods, dots).
    - `HitTestStroke()`: Two-tier  circle hit-test combining register-level bounding box culling
    - `HitTestContinuousSweptCapsule()`: Full continuous swept-capsule collision implementation using Dan Sunday's segment-to-segment minimum distance algorithm. Sweeps the eraser motion vector against the stroke segment, guaranteeing **zero tunneling / skipping** even during high-velocity eraser strokes.
  - **`StrokeSlicer`:**
    - Vector erasing utility.
    - Employs an  quadratic circle-segment intersection formulation:
      $$\|\vec{p}(t) - \vec{c}_{eraser}\|^2 = R_{eraser}^2$$
    - Analytically solves for parametric entry/exit parameters $t \in [0.0, 1.0]$, splits pierced segments, and partitions surviving portions into clean, independent sub-strokes with reconstructed terminal end caps.

