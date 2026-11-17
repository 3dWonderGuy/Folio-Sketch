# Folio-Sketch Core Math Engine (Tier 1)

## Scope & Architectural Boundaries

`Folio-Sketch` Tier 1 is a pure headless C++20 ink modeling and computational geometry engine housed under `namespace Folio`. It is designed with strict modular separation:
- **Zero Graphics & Windowing Dependencies:** No OpenGL, Vulkan, Direct3D, X11, Wayland, or Win32 headers.
- **Zero OS Dependencies:** Pure algorithmic C++20 (`std::span`, `<chrono>`, `<cmath>`, `<numbers>`), operating identically on Linux, Android, iOS, macOS, and Windows.
- **Deterministic Math Core:** Completely decoupled from frame loops, render buffers, or surface lifecycles.

```
       +-------------------------------------------------------------+
       |                     Raw Stylus Samples                      |
       |  (Screen coords -> Physical mm, Pressure, Tilt, Nanoseconds)|
       +-------------------------------------------------------------+
                                      |
                                      v
       +-------------------------------------------------------------+
       | Stage 1: Calibration & Low-Speed Tremor Cancellation        |
       |  - Deadzone & Saturation Clamping                           |
       |  - Pressure Gamma Power-Law Curve                           |
       |  - Low-Speed Hand Tremor Attenuation (1.31 -> 1.44 mm/s)    |
       +-------------------------------------------------------------+
                                      |
                                      v
       +-------------------------------------------------------------+
       | Stage 2: Second-Order Spring-Mass-Damper ODE Solver         |
       |  - Google Ink Modeler Physics (k/m ~ 0.000339, gamma = 72.0)|
       |  - Intermediate Angular Upsampling (> 3.4 degrees)          |
       |  - Pen-Up Relaxation Settling Convergence (< 0.01 mm)       |
       +-------------------------------------------------------------+
                                      |
                                      v
       +-------------------------------------------------------------+
       | Stage 3: Dynamic Width Evaluation & Prediction              |
       |  - Asymmetric Acceleration Limiting (3500 vs 2500 mm/s^2)   |
       |  - Corner Turn-Rate Bloat Compensation (dTheta > 30 deg)    |
       |  - Spatial Cone-Slope Clamp (dr/ds <= 0.35 mm/mm)           |
       |  - Forward Kalman Predictor (Curvature-gated, Tapered Tip)  |
       +-------------------------------------------------------------+
                                      |
                                      v
             +------------------------+------------------------+
             |                                                 |
             v                                                 v
  +----------------------+                          +----------------------+
  |     Live Output      |                          |   Terminal Output    |
  |  Incremental Stream  |                          |   Committed Stroke   |
  |   std::span<const    |                          |  FinishedStrokeData  |
  |   CapsuleInstance>   |                          |   (RDP Decimation,   |
  |  (32-byte GPU PODs)  |                          |  Gaussian Smoothing, |
  +----------------------+                          |   Vector InkPath)    |
                                                    +----------------------+
```

---

## Data Boundaries

### Ingestion: `StylusInputPoint`
Hardware stylus and touch inputs are passed directly to `Folio::InkEngine` as raw physical measurements:
```cpp
struct StylusInputPoint {
    double  x            = 0.0;   // Physical horizontal coordinate in millimeters
    double  y            = 0.0;   // Physical vertical coordinate in millimeters
    float   pressure     = 1.0f;  // Normalized stylus pressure in [0.0, 1.0]
    double  timeSeconds  = 0.0;   // Timestamp (seconds)
    float   speed        = 0.0f;  // Speed (mm/s)
    float   tiltX        = 0.0f;  // Stylus X tilt angle in radians
    float   tiltY        = 0.0f;  // Stylus Y tilt angle in radians
    int64_t timeNs       = 0;     // Hardware timestamp in nanoseconds
    float   tiltRad      = 0.0f;  // Stylus tilt altitude in radians
};
```
Operating in physical millimeters (`mm`) guarantees that stroke width, smoothing, and physics remain invariant to screen DPI, pixel density, or display scaling.

### Live Output: `std::span<const CapsuleInstance>`
During an active stroke, the engine emits dynamic continuous updates as contiguous 32-byte GPU-ready POD capsules:
```cpp
struct alignas(16) CapsuleInstance {
    float pointA[2];          // Segment start center (x, y) in canvas points
    float pointB[2];          // Segment end center (x, y) in canvas points
    float radiusA;            // Radius at pointA
    float radiusB;            // Radius at pointB
    uint32_t colorRgba;       // Start color packed as 0xAABBGGRR
    uint32_t colorRgbaEnd;    // End color packed as 0xAABBGGRR (for gradients)
};
static_assert(sizeof(CapsuleInstance) == 32, "CapsuleInstance must be 32 bytes");
```
This payload is streamed directly into persistent VBOs with zero CPU vertex expansion or allocation overhead.

### Terminal Output: `FinishedStrokeData`
Upon pen-up (`OnPointerUp`), the stroke finishes physical simulation and is serialized into an archival format:
- **Decimated Centerline:** RDP-reduced polyline vertices.
- **Smoothed Radii:** Spatially convolved width samples.
- **Bounding AABB:** Conservative geometric axis-aligned bounding box for spatial partitioning, BVH insertion, and dirty tile invalidation.
- **Vector `InkPath`:** Cubic Bézier outlines for lossless SVG export or high-resolution zoom rasterization.

---

## The 4-Stage Core Inking Pipeline

### Stage 1: Calibration & Low-Speed Wobble Filtering

Raw digitizer sensors suffer from mechanical quantization jitter, sensor noise, and human neuromuscular hand tremors.

1. **Deadzone & Saturation:**
   Raw pressure $p_{raw}$ is clamped to $[p_{min}, p_{max}]$:
   $$\hat{p} = \text{clamp}\left(\frac{p_{raw} - p_{min}}{p_{max} - p_{min}}, 0.0, 1.0\right)$$
2. **Power-Law Gamma Curve:**
   Stylus tip force is linearized for human perception via adjustable gamma:
   $$p = \hat{p}^{\gamma_{pressure}}$$
   Typical values: $\gamma = 1.0$ (linear), $\gamma = 1.5$ (fine calligraphy control), $\gamma = 0.7$ (expressive sketching).
3. **Low-Speed Hand Tremor Cancellation:**
   When writing slowly (e.g. dotting an "i" or slow line hatching), digitizers report high-frequency micro-wobbles. The engine applies an adaptive low-pass filter calibrated between two speed thresholds:
   - $v_{floor} = 1.31\text{ mm/s}$ (Full hand-tremor attenuation)
   - $v_{ceiling} = 1.44\text{ mm/s}$ (Pass-through unattenuated natural stroke dynamics)
   For speeds $v \le v_{floor}$, position updates are aggressively smoothed against the running centroid. Between $v_{floor}$ and $v_{ceiling}$, a smooth hermite ease-in transitions from filtering to dynamic physics.

---

### Stage 2: Second-Order Spring-Mass-Damper ODE Solver

Folio-Sketch implements Google Ink's continuous physical mass-spring-damper formulation to track the digitizer position:

$$\vec{F}_{spring} = -k (\vec{x}_{mass} - \vec{x}_{target})$$
$$\vec{F}_{damping} = -\gamma \vec{v}_{mass}$$
$$\frac{d^2\vec{x}}{dt^2} = -\frac{k}{m} (\vec{x} - \vec{x}_{target}) - \frac{\gamma}{m} \vec{v}$$

#### Calibrated Constants
- **Stiffness-to-mass ratio ($k/m$):** $\approx 0.000339\text{ ns}^{-2}$
- **Damping ratio ($\gamma/m$):** $\approx 72.0\text{ s}^{-1}$ (critically damped to prevent overshoot ringing while maintaining instant response).

#### Intermediate Angular Upsampling
If the angle between consecutive input vectors exceeds $\theta_{thresh} = 3.4^\circ$ ($0.0593\text{ rad}$), the digitizer is under-sampling a tight curve (e.g., cursive loops or handwriting ligatures). The engine automatically subdivides the step into intermediate sub-steps, evaluating the ODE iteratively to eliminate angular corner cuts.

#### Pen-Up Relaxation Settling
When the physical stylus leaves the screen (`OnPointerUp`), the physical mass does not freeze mid-air. The engine continues stepping the ODE forward until the residual spring velocity drops below the convergence tolerance:
$$\|\vec{x}_{mass} - \vec{x}_{target}\| < 0.01\text{ mm}$$
This draws the stroke cleanly to its true physical resting point without blunt truncated tips.

---

### Stage 3: Dynamic Width Evaluation & Prediction

#### Asymmetric Acceleration Filtering
Natural pen movement exhibits different mechanical dynamics during acceleration versus braking. To prevent unnatural sudden thinnings:
- **Max Acceleration:** $3500\text{ mm/s}^2$
- **Max Deceleration:** $2500\text{ mm/s}^2$
This asymmetry stabilizes the velocity signal, keeping stroke widths predictable during expressive calligraphy flicks.

#### Corner Turn-Rate Bloat Compensation
When writing cursive or signing, handwriting naturally slows down in corners. Unfiltered velocity thinning causes sharp turns to bloat into huge puddles of ink. Folio-Sketch detects the turn angle:
$$\Delta\theta = \arccos\left(\frac{\vec{v}_1 \cdot \vec{v}_2}{\|\vec{v}_1\|\|\vec{v}_2\|}\right)$$
When $\Delta\theta > 30^\circ$, the evaluator dynamically blends the instantaneous corner speed toward the stroke's cruising speed, preventing corner puddling while maintaining fluid transitions.

#### Spatial Cone-Slope Clamp
Physical nibs cannot instantly change their geometric contact area. Between consecutive centerline vertices separated by arc length $\Delta s$, the radius rate-of-change is strictly bounded:
$$\left|\frac{\Delta r}{\Delta s}\right| \le 0.35\text{ mm/mm}$$
This prevents stepping artifacts, sharp notches, or geometric self-intersections.

#### Forward Kalman Predictor
To compensate for input scanning and display pipeline latency, a forward Kalman state predictor projects the stroke tip ahead in time:
- **Curvature Cutoff:** If path curvature $\Delta\theta > 30^\circ$, prediction is immediately gated to zero to prevent the predicted tip from overshooting around tight corners.
- **Tip Tapering:** The predicted extension tapers smoothly down to $30\%$ of the base stroke radius.
- **Alpha Attenuation:** A distance-weighted alpha fade dissolves the predicted tip into the background, ensuring invisible transitions as real hardware samples arrive.

---

### Stage 4: Post-Processing Pipeline

When a stroke completes, it is transitioned from live rendering to permanent document storage through two non-destructive refinement passes:

#### 1. Ramer-Douglas-Peucker (RDP) Chord Decimation
Redundant collinear points generated by 240Hz/480Hz digitizer sampling are decimated:
- Bounded perpendicular distance $\epsilon = 0.08\text{ mm}$.
- Point reduction: **$70\%\text{--}85\%$ fewer control points**, reducing document memory footprint while preserving exact optical contouring.

#### 2. Two-Pass Spatial Gaussian Radius Smoothing
Radius fluctuations induced by slight stylus grip pressure noise are smoothed using a normalized 1D spatial Gaussian kernel:
$$G(s) = \frac{1}{\sqrt{2\pi}\sigma} e^{-\frac{s^2}{2\sigma^2}}, \quad \sigma = 3.5\text{ mm}$$
The two-pass spatial convolution runs over arc length $s$ (rather than index spacing), ensuring invariant smoothing regardless of point density.
