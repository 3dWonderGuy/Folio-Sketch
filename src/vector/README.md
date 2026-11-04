# FolioSketch Vector Module (`src/vector` & `include/folio_sketch/vector`)

This directory contains the CPU vector contour generation pipeline, line styling, pattern engines (dashed, dotted, textures), and vector path representation (`InkPath`).

---

### Pipeline Architecture

```
[StrokePoint Sequence]
         │
         ▼
 1. InkGenerator::GenerateStroke()   (Top-level dispatcher)
         ├─────────────────────────────────────────┐
         ▼ (Open stroke / freehand ink)            ▼ (Closed geometric polygon)
 2. BuildStroke()                          2. BuildPolygon()
    • Left/Right boundary offset rails        • Smooth continuous loop wrap-around
    • Terminal caps (Round/Chisel/Square)     • Round miter corner joins
         │                                         │
         └────────────────────┬────────────────────┘
                              ▼
 3. Pattern Engine (ApplyPattern)
    • Solid, Dashed, Dotted, DashDot, Pencil
                              ▼
 4. (Optional) Infill Engine (BuildInfill)
    • Solid interior fill, Drafting hatches
                              ▼
                        [InkPath]
     (MoveTo, LineTo, CubicTo, Close vector commands)
                              │
               ┌──────────────┴──────────────┐
               ▼                             ▼
        [SVG / PDF Export]        [GPU Stencil / Triangulation]
```

---

### File Inventory & Roles

| File | Role | Description |
| :--- | :--- | :--- |
| `include/folio_sketch/vector/ink_path.hpp` | **Vector Path Type** | Defines `InkPath`, `PathPoint`, `PathVerb` (`MoveTo`, `LineTo`, `CubicTo`, `Close`), and bounding box utilities. Compatible with vector engines like ThorSVG and Blend2D. |
| `include/folio_sketch/vector/ink_generator.hpp` | **Generator API** | Public interface declaring `InkGenerator` methods for strokes, polygons, infill hatches, and pattern tessellation. |
| `src/vector/ink_generator_polygon.cpp` | **Dispatcher & Polygons** | Top-level `GenerateStroke` dispatcher and closed polygon outline generator with seamless loop closure. |
| `src/vector/ink_generator_stroke.cpp` | **Open Stroke Ribbon** | Extrudes left and right offset rails along centerline points and attaches terminal caps. |
| `src/vector/ink_generator_caps.cpp` | **Endcap Geometry** | Generates endpoint geometry for `CapType::Round`, `CapType::Square`, and `CapType::Chisel`. |
| `src/vector/ink_generator_patterns.cpp` | **Pattern Engine** | Subdivides continuous outlines into dashed, dotted, or dash-dot segments based on arc length. |
| `src/vector/ink_generator_infill.cpp` | **Infill & Hatches** | Generates interior polygon fill geometry and technical drafting hatch lines (diagonal, crosshatch). |
| `src/vector/ink_ribbon_corner.cpp` | **Corner Geometry** | Evaluates sharp corner miter clamping, bevel joins, and inner-rail corner adapters. |
