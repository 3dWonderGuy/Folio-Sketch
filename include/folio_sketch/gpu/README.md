# FolioSketch GPU Module

This directory contains backend-agnostic GPU data layouts, vertex formats, and instance streaming pipelines for real-time ink display and geometric shape rendering.

---

### Key Components in `ink_mesh.hpp`

#### 1. Inking Pipeline (`CapsuleInstance` & `CapsuleBuilder`)
* **`CapsuleInstance`**: Universal per-segment instance data layout for GPU Signed Distance Field (SDF) renderers (start/end positions, radii, and color). Can be bound directly into vertex/instance buffers by any graphics API (SDL_gpu, Vulkan, Metal, DirectX 12, OpenGL).
* **`CapsuleBuilder`**: Generates `CapsuleInstance` streams in $O(N)$ time with zero dynamic allocations inside the streaming loop.

#### 2. Shape Pipeline (`GpuShapeInstance` & `ShapeBuilder`)
* **`GpuShapeInstance`**: Universal per-shape instance data layout for GPU Signed Distance Field (SDF) shape rendering (center, size, corner radius, stroke width, fill/stroke colors, and `ShapeKind`).
* **`ShapeBuilder`**: Helper to construct `Rectangle`, `RoundedRectangle`, `Circle`, and `Ellipse` GPU instances.

#### 3. Color Packing
* **`PackGpuColor`** / **`PackPremultipliedGpuColor`**: Packs 32-bit RGBA colors for GPU pipelines.

#### 4. Legacy Triangle Mesh (`InkMesh` / `InkVertex`) — *Deprecated*
* Traditional indexed triangle mesh representation. Marked deprecated in favor of `CapsuleInstance` (for inking) and `GpuShapeInstance` (for shapes). Kept for benchmark and fallback comparisons.

---

> [!NOTE]
> Backend-specific reference renderers (`GlCapsuleRenderer` for inking and `GlShapeRenderer` for geometric shapes) are located under `src/debug/` for testing inside `folio_ink_inspector`.
