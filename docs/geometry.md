# Computational Geometry & Mathematical Primitives

The geometry subsystem ([`src/geometry/`](../src/geometry/) and [`include/folio_sketch/ink_stroke_data.hpp`](../include/folio_sketch/ink_stroke_data.hpp)) provides low-level mathematical primitives, vector algebra, segment tangent/normal computation, segment-segment intersection tests, swallowtail loop removal, and oriented nib models.

---

## 1. Segment Representation (`SegmentInfo`)

Given consecutive centerline modeled points $P_i$ and $P_{i+1}$, the displacement vector is:
$$\vec{d} = P_{i+1} - P_i = (\Delta x, \Delta y)$$
$$\text{len} = \|\vec{d}\| = \sqrt{\Delta x^2 + \Delta y^2}$$

Unit tangent vector $\vec{u}$:
$$\vec{u} = \left(\frac{\Delta x}{\text{len}}, \frac{\Delta y}{\text{len}}\right)$$

Unit normal vector $\vec{n}$ (rotated $90^\circ$ counter-clockwise in screen coordinates where $+Y$ points downward):
$$\vec{n} = (\vec{u}_y, -\vec{u}_x)$$

By convention:
- **Left rail offset**: $P_{left} = P + \vec{n} \cdot r$
- **Right rail offset**: $P_{right} = P - \vec{n} \cdot r$

---

## 2. 2D Segment-Segment Intersection (`IntersectSegments`)

Intersection between segment $S_1 = [P_1, P_2]$ and segment $S_2 = [P_3, P_4]$.

### Step 1: Axis-Aligned Bounding Box (AABB) Pre-Rejection
Before evaluating algebraic determinants, AABBs are compared with a tolerance $\epsilon = 10^{-6}$:
$$\max(P_{1x}, P_{2x}) < \min(P_{3x}, P_{4x}) - \epsilon \implies \text{No Intersection}$$
$$\min(P_{1x}, P_{2x}) > \max(P_{3x}, P_{4x}) + \epsilon \implies \text{No Intersection}$$
(Repeated symmetrically for the $Y$ axis).

### Step 2: 2D Determinant (Cross Product)
Let:
$$\vec{d}_1 = P_2 - P_1, \quad \vec{d}_2 = P_4 - P_3, \quad \vec{\Delta} = P_3 - P_1$$
$$\text{denom} = \vec{d}_{1x} \vec{d}_{2y} - \vec{d}_{1y} \vec{d}_{2x}$$

If $|\text{denom}| < 10^{-9}$, segments are parallel or collinear (no intersection returned).

The parametric scalars $u$ along $S_1$ and $v$ along $S_2$:
$$u = \frac{\vec{\Delta}_x \vec{d}_{2y} - \vec{\Delta}_y \vec{d}_{2x}}{\text{denom}}$$
$$v = \frac{\vec{\Delta}_x \vec{d}_{1y} - \vec{\Delta}_y \vec{d}_{1x}}{\text{denom}}$$

- **Inclusive Mode** (swallowtail loop pruning): $u, v \in [-10^{-5}, 1 + 10^{-5}]$.
- **Interior-Only Mode** (self-loop infill detection): $u, v \in [10^{-4}, 1 - 10^{-4}]$. Avoids spurious intersections at shared segment endpoints.

The intersection point:
$$P_{hit} = P_1 + u \vec{d}_1$$

---

## 3. Swallowtail Pinch Loop Pruning (`PruneSwallowtailLoops`)

### The Mathematical Cause of Swallowtails
When a variable-width ribbon sweeps around a turn where the path's radius of curvature $R$ is smaller than the stroke half-width $r$ ($R < r$), the inner offset curve develops a mathematical singularity (cusp) and forms a self-intersecting self-crossing loop known as a **swallowtail**.

```text
    Path:     ───────╮
                     │ R < r
                     ╰───────
    Inner Rail: ───╮ ╭───
                    ╳   <-- Swallowtail self-crossing loop
                   ╭─┴─╮
```

### The Pruning Algorithm
1. Iterate through rail vertices $i \in [0, N-3]$.
2. Search forward within a local window $j \in [i+2, \min(N-2, i + \text{maxLoopSpan})]$ from back to front.
3. Test intersection between segment $[P_i, P_{i+1}]$ and segment $[P_j, P_{j+1}]$.
4. If an intersection $P_{hit}$ is found:
   - Clamp the start vertex: $P_{i+1} \leftarrow P_{hit}$.
   - Erase the retrograde loop vertices between $i+2$ and $j$:
     $$\text{rail.erase}(P_{i+2}, P_{j+1})$$
5. Continue scanning until all local self-crossing loops are eliminated, leaving a monotonic topological boundary.

---

## 4. Oriented Nib Profiles (`ink_nib_geometry.hpp`)

Different pen types project distinct boundary geometries:

| Cap Type | Geometry Model | Mathematical Formulation |
| :--- | :--- | :--- |
| **`Round`** | Regular 32-gon | $P(\theta) = (r \cos\theta, r \sin\theta)$ for $\theta \in [0, 2\pi)$ |
| **`Square`** | Rotated Rectangle (Highlighter blade) | Dimensions $0.9r \times 2.6r$ oriented at $90^\circ$ relative to drawing axis |
| **`Chisel`** | Calligraphy diamond | Elliptic/rhombic blade fixed at $45^\circ$ angle |

### Ribbon Support Function
For non-circular nibs (e.g. Chisel), the offset at segment normal $\vec{n}$ is computed using the support function of the polygon $K$:
$$h_K(\vec{n}) = \sup_{p \in K} \langle p, \vec{n} \rangle$$
This finds the extreme vertices on the nib profile that lie furthest in the direction of the rail normal, producing clean variable-width calligraphic modulation without manual curve approximations.
