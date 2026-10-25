# Vector Ribbon & Infill Generator

The vector generator ([`src/generator/`](../src/generator/) and [`include/folio_sketch/ink_generator.hpp`](../include/folio_sketch/ink_generator.hpp)) converts discrete modeled stroke points into continuous, clean 2D vector boundaries represented as [`InkPath`](../include/folio_sketch/ink_path.hpp) structures, ready for direct rendering or ThorSVG shape conversion.

---

## 1. Miter-Clipped Single Contour Ribbon (`BuildSolidRibbon`)

To avoid winding-rule artifacts, multiple overlapping polygons, and rendering seams in translucent strokes (highlighters/markers), `BuildSolidRibbon` generates a **single continuous boundary contour**:

1. **Forward Pass (Left Rail)**: Traces offset points along the left side of the stroke from head to tail.
2. **Tail Endcap**: Sweeps a half-circle or oriented nib cap around the final point.
3. **Backward Pass (Right Rail)**: Traces offset points along the right side of the stroke from tail back to head.
4. **Head Endcap**: Sweeps a closure cap at the start point, creating a closed watertight vector loop.

```text
    Head Cap ───> [ Left Rail Forward ] ───> Tail Cap
       ^                                         │
       └────────── [ Right Rail Backward ] <─────┘
```

---

## 2. Corner Bisector & Miter Projection Derivation

At a corner vertex $P_i$ between incoming unit tangent $\vec{u}_{in}$ and outgoing unit tangent $\vec{u}_{out}$:

### Tangent Half-Angle Vector (Angle Bisector)
$$\vec{u}_{sum} = \vec{u}_{in} + \vec{u}_{out}$$
$$\cos\left(\frac{\theta}{2}\right) = \sqrt{\frac{1 + \vec{u}_{in} \cdot \vec{u}_{out}}{2}}$$
$$\sin\left(\frac{\theta}{2}\right) = \sqrt{\frac{1 - \vec{u}_{in} \cdot \vec{u}_{out}}{2}}$$

The unit bisector vector pointing along the miter direction is:
$$\vec{b} = \frac{\vec{u}_{sum}}{\|\vec{u}_{sum}\|}$$

The theoretical miter length $M$ required for the edge to remain flush is:
$$M = \frac{r}{\sin(\theta/2)}$$

### Safe Segment Length Limit
If $M$ exceeds adjacent segment lengths, the miter will overshoot and penetrate neighboring edges. To guarantee topological sanity, the projection distance is clamped by the **Safe Segment Limit**:
$$\text{safeLimit} = \frac{\min(L_{in}, L_{out})}{2 \sin(\theta/2)}$$
$$M_{actual} = \min(M, \text{safeLimit})$$

### Corner Classification & Blending
Depending on the deflection angle $\theta$:
1. **Gentle Curves ($\theta \le 60^\circ$)**: Perfect angle bisector miter is safe and projected directly.
2. **Sharp Corners ($60^\circ < \theta \le 150^\circ$)**: A 3-point circular fillet or round sweep fan is inserted along the outer edge.
3. **Hairpin Turnarounds ($\theta > 150^\circ$)**: Reversal elbow normals bow outward continuously in the direction of stroke motion without splitting or pinching.

---

## 3. Watertight Cavity Infill (`DetectStrokeSelfLoop`)

Folio-Sketch provides automatic vector loop detection for flood-fill paint tools.

### Detection Pipeline
1. **Intersection Scan**: Evaluates non-adjacent segment pairs $(S_i, S_j)$ with $|i - j| \ge 2$ using `IntersectSegments(inclusive = false)`.
2. **Cavity Extraction**: When an intersection $P_{hit}$ is identified:
   - Slices centerline points between indices $i+1$ and $j$.
   - Inserts $P_{hit}$ as both the starting and closing vertex, producing an independent, closed `InkPath`.
3. **Dual Safety Area Validation (Shoelace Formula)**:
   $$\text{Area} = \frac{1}{2} \left| \sum_{k=0}^{M-1} (x_k y_{k+1} - x_{k+1} y_k) \right|$$
   - **Minimum Area Check ($A \ge \text{minArea}$)**: Rejects tiny micro-loops, pen jitter, and unintentional crossovers.
   - **Maximum Area Check ($A \le \text{maxArea}$)**: Prevents accidental flooding of the entire canvas boundary.
4. **Non-Destructive Infill**:
   - Returns a `LoopInfillResult` containing `hasLoop`, `loopPath`, `intersectionPoint`, and `loopAreaMm2`.
   - The original open ink stroke is preserved completely untouched.

```cpp
LoopInfillResult result = InkGenerator::DetectStrokeSelfLoop(
    strokePoints,
    /*minAreaMm2=*/ 2.0f,
    /*maxAreaMm2=*/ 50000.0f
);

if (result.hasLoop) {
    // Fill the closed cavity with color/pattern without altering the original stroke
    auto fillShape = result.loopPath.toThorVGShape();
}
```

---

## 4. Visual Vector Debugger (`DumpDebugSvg`)

To verify geometric invariants and inspect rail construction, `InkGenerator::DumpDebugSvg` exports a color-coded vector diagnostic SVG:

| Color | Element | Description |
| :--- | :--- | :--- |
| **Cyan** | Centerline | Modeled stroke spine |
| **Yellow Circles** | Node Radii | Effective width at each modeled vertex |
| **Green** | Left Rail | Forward offset boundary |
| **Blue** | Right Rail | Backward offset boundary |
| **Red** | Stroke Fill | Semi-transparent rendered contour |
| **Red Crosshairs** | Defect Hits | Segment-segment boundary self-intersections |
| **Magenta Lines** | Retrograde Steps | Boundary vertices that step backwards against local flow |
