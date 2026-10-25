# GPU Mesh Builder & Anti-Aliasing Skirts

The mesh builder subsystem ([`src/mesh/ink_mesh_builder.cpp`](../src/mesh/ink_mesh_builder.cpp) and [`include/folio_sketch/ink_mesh.hpp`](../include/folio_sketch/ink_mesh.hpp)) generates direct, hardware-accelerated indexed triangle meshes (`InkMesh`) from modeled centerline points.

By bypassing CPU polygon rasterizers and clipping libraries (e.g. Clipper2, Libtess2), `InkMeshBuilder` achieves sub-millisecond GPU tessellation speeds suitable for low-latency live inking and high-refresh-rate displays ($120\text{ Hz}$).

---

## 1. Vertex Format (`InkVertex`)

```cpp
struct InkVertex {
    float x = 0.0f;        // 2D X coordinate in world/canvas space (mm or px)
    float y = 0.0f;        // 2D Y coordinate in world/canvas space
    float edgeDist = 0.0f; // Normalized distance to centerline: 0.0 at spine, 1.0 at outer rim, >1.0 at AA skirt
    float radius = 0.0f;   // Physical radius at this point
    uint32_t color = 0;    // Packed RGBA32 vertex color
};
```

---

## 2. Dual-Buffer Partitioning (Core vs. AA Skirt)

To support crisp, antialiased rendering on GPU backends without requiring full-screen multi-sample anti-aliasing (MSAA), `InkMesh` partitions its index buffer into two consecutive regions:

```text
outMesh.indices:
┌─────────────────────────────────┬─────────────────────────────────┐
│     Core Triangle Indices       │    AA Skirt Triangle Indices    │
│    [0 ... coreIndexCount - 1]   │    [coreIndexCount ... End]     │
└─────────────────────────────────┴─────────────────────────────────┘
```

1. **Core Geometry (`coreIndices`)**:
   - Spans the interior ribbon quads, corner fans, and circular/nib endcaps.
   - Vertices have `edgeDist` ranging from $0.0$ (centerline spine) to $1.0$ (nominal stroke outer boundary).
   - Rendered as fully opaque or with nominal stroke alpha.

2. **Exterior Anti-Aliasing Skirt (`skirtIndices`)**:
   - An expanded boundary band extruded outward by $\delta$:
     $$\delta = \frac{3.0\text{ px}}{\text{scalePxPerMm}}$$
   - Skirt vertices are assigned an extended distance parameter:
     $$\text{edgeDist}_{skirt} = 1.0 + \frac{\delta}{r}$$
   - In GPU fragment shaders, alpha can be smoothly faded from $1.0$ at `edgeDist = 1.0` down to $0.0$ at `edgeDist = skirtDist`, achieving smooth, hardware-accelerated analytic anti-aliasing.

---

## 3. Ribbon Quad Generation & Corner Fans

For each segment from $P_i$ to $P_{i+1}$:

### Segment Quads
Two triangles are emitted for the core quad connecting $P_i$ and $P_{i+1}$ along the left and right normals:
- Left Core Quad: $(P_{i,left}, P_{i,center}, P_{i+1,center})$ and $(P_{i,left}, P_{i+1,center}, P_{i+1,left})$
- Right Core Quad: $(P_{i,center}, P_{i,right}, P_{i+1,right})$ and $(P_{i,center}, P_{i+1,right}, P_{i+1,center})$

Corresponding exterior skirt quads are extruded outward along each outer rail:
- Skirt Quad: $(P_{i,rail}, P_{i,skirt}, P_{i+1,skirt})$ and $(P_{i,rail}, P_{i+1,skirt}, P_{i+1,rail})$

```text
       P_{i,left_skirt}      P_{i+1,left_skirt}
             ▲                      ▲
             │      Left Skirt      │
             ▼                      ▼
       P_{i,left} ──────────── P_{i+1,left}
             │                      │
             │      Core Left       │
             ▼                      ▼
       P_{i,center} ────────── P_{i+1,center}  <-- Spine (edgeDist = 0.0)
             ▲                      ▲
             │      Core Right      │
             ▼                      ▼
       P_{i,right} ─────────── P_{i+1,right}
             ▲                      ▲
             │     Right Skirt      │
             ▼                      ▼
       P_{i,right_skirt}     P_{i+1,right_skirt}
```

### Corner Fans at Acute Turns
When turning sharply:
- The inner rail contracts to an inner corner vertex.
- The outer rail sweeps an arc fan with dynamic step resolution:
  $$\text{fanSteps} = \text{clamp}\left(\left\lceil \frac{\theta}{\pi / 16} \right\rceil, 2, 16\right)$$
- An accompanying skirt fan wraps around the outside of the arc, ensuring complete anti-aliasing continuity around corners.

---

## 4. Single-Point Dots & Oriented Polygonal Nibs

When a stroke consists of a single point (tap/dot):
- **Round Nib**: Generates a 32-step regular polygon circle fan connected to a central vertex, bounded by a 32-step exterior skirt ring.
- **Square & Chisel Nibs**: Emits oriented nib polygons (`GetNibVertices`) with corresponding expanded outer boundary skirts.

---

## 5. Memory Efficiency & Zero Allocations

`InkMesh::clear()` resets vector counts without deallocating capacity (`std::vector::clear()`). Repeated calls to `InkMeshBuilder::BuildSolidMesh` during live inking reuse vertex and index buffer memory buffers across frames, eliminating heap fragmentation and GC pauses on embedded/mobile devices.
