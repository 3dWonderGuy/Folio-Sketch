# FolioSketch Vector Module

This directory provides 2D vector path structures and contour ribbon generation for export (SVG, PDF, Blend2D, ThorSVG).

---

### 1. `ink_path.hpp`
* **What it does:** Defines a lightweight 2D vector path container and interchange types.
* **Key components:**
  - `PathVerb`: Standard path commands (`MoveTo`, `LineTo`, `CubicTo`, `QuadTo`, `Close`).
  - `InkPath`: Verb and point stream representing vector contours, including bounding box calculations and SVG path data export (`toSvgPathData()`).

---

### 2. `ink_generator.hpp`
* **What it does:** Constructs continuous 2D boundary polygons from smoothed centerline points.
* **Key components:**
  - `InkGenerator`: Tessellates strokes into filled polygon outlines.
  - Supports stroke cap styles (`Round`, `Flat`), corner join styles, and line patterns (solid, dashed, dotted, dash-dot).
  - Supports interior pattern infills and hatching for recognized closed shapes.
