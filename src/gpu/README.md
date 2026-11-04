# FolioSketch GPU & Mesh Module (`src/gpu` & `include/folio_sketch/gpu`)

This module provides hardware-accelerated vertex formats, instance memory layouts, and data streaming builders for real-time stroke and shape rendering.

---

### Pipeline Architecture

```
[Modeled Stroke Points]
          │
          ▼
   CapsuleBuilder::BuildCapsuleInstances()   (O(N) CPU streaming, zero allocation overhead)
          │
          ▼
  CapsuleInstance Buffer                    (Streamed to GPU VBO)
          │
          ▼
 [GPU Fragment Shader]                      (Analytic Euclidean Signed Distance Field)
          │
          ▼
  Pixel Display                             (Perfect round caps, joins, and AA at any zoom)
```

---

### File Inventory & Roles

| File | Role | Description |
| :--- | :--- | :--- |
| `include/folio_sketch/gpu/ink_mesh.hpp` | **Hardware Type Definitions** | Defines color packing (`PackGpuColor`, `PackPremultipliedGpuColor`), the `CapsuleInstance` GPU memory layout, and the `CapsuleBuilder` interface. Also contains the legacy `InkMesh` and `InkVertex` triangle structures. |
| `src/gpu/ink_mesh_builder.cpp` | **Instance Streaming Builder** | Converts centerline `StrokePoint` sequences into contiguous `CapsuleInstance` streams in $O(N)$ time with zero dynamic allocations inside the inner loop. |

---

### The Two Rendering Paradigms

1. **Analytic Capsule SDF (`CapsuleInstance` - Primary & Active):**
   * Uses per-segment instance attributes: `pointA`, `pointB`, `radii` $(r_A, r_B)$, and packed RGBA color.
   * GPU draws an oriented bounding quad per segment and evaluates the distance analytically in the fragment shader.
   * Eliminates CPU mesh tessellation, bowtie quad self-intersections, and overlapping seam artifacts.
2. **Traditional Triangle Mesh (`InkMesh` - Legacy):**
   * Indexed triangle mesh (`vertices`, `indices`) computed via CPU offset rails.
   * Kept primarily for benchmark comparisons in the diagnostic inspector (`folio_ink_inspector`).
