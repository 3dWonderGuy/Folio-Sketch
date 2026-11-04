# FolioSketch Geometry Module (`src/geometry`)

This directory contains 2D computational geometry utilities, centerline curve conditioning, nib profile mathematics, and topological defect cleaners used across stroke generation and vector export.

---

### File Inventory & Responsibilities

| File | Role | Description |
| :--- | :--- | :--- |
| `ink_stroke_filter.hpp` / `.cpp` | **Centerline Conditioning** | Pre-filters centerline points prior to polygon expansion. Eliminates redundant containment disks (points completely enveloped by neighboring disks), regularizes radius slope ($\|dr/ds\| \le \text{maxSlope}$ to prevent abrupt thickness jumps), and detects hairpin sharp turns. |
| `ink_geometry_utils.hpp` / `.cpp` | **2D Math & Topology Cleaning** | Core 2D utilities: computes segment tangent/normal vectors, performs fast segment-segment intersection tests with AABB culling, detects self-intersections, and prunes swallowtail loops on tight curves ($R < r$). |
| `ink_nib_geometry.hpp` | **Nib Footprints & Offsets** | Computes oriented geometric footprints for different stylus nib shapes (`Round`, `Square`/Highlighter blade, and `Chisel`/Calligraphy 45° angle) and evaluates extreme boundary offset vectors $(n_x, n_y)$ for ribbon extrusion. |

---

### Relationship to Public Geometry Headers (`include/folio_sketch/geometry/`)

* **`include/folio_sketch/geometry/shape_recognizer.hpp`**:
  The public API for shape recognition and snapping. Evaluates finished centerline curves to classify and fit geometric primitives (straight lines, circles, ellipses, triangles, rectangles, squares) and outputs synthetic snapped `StrokePoint` sequences.
* **`src/geometry/` (Internal)**:
  Contains the lower-level geometric building blocks (tangents, normals, intersections, loop pruning, nib profiles) consumed internally by `src/vector/` (the ribbon & polygon generators) and shape snapping.
