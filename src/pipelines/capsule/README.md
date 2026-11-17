# Capsule SDF Pipeline (`src/pipelines/capsule`)

This pipeline converts modeled stroke centerlines (`StrokePoint`) and geometric shapes directly into compact, GPU-instanced Signed Distance Field (SDF) primitives (`CapsuleInstance` and `GpuShapeInstance`) in $O(N)$ time (or $O(1)$ incrementally per live sample).

---

### Files

* **`ink_capsule.hpp`**:
  - `CapsuleInstance` (32-byte POD): Segment endpoints $(P_A, P_B)$, tapered radii $(r_A, r_B)$, and premultiplied start/end RGBA8 colors.
  - `GpuShapeInstance`: Analytic SDF descriptors for rectangles, rounded rectangles, circles, and ellipses.
  - `CapsuleBuilder` & `ShapeBuilder`: Zero-overhead instance assemblers.
* **`ink_capsule_builder.cpp`**:
  - Full-stroke and incremental $O(1)$ capsule emission (`BuildCapsuleInstances` and `AppendCapsuleInstances`).
