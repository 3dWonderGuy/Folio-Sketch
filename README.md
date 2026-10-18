# Folio-Sketch
*High-Performance Computational 2D Vector Inking Engine Library*

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()
[![Backend: Blend2D](https://img.shields.io/badge/Backend-Blend2D-orange.svg)](https://blend2d.com)

## Overview
**Folio-Sketch** is a standalone, hardware-accelerated 2D computational vector geometry and inking engine. It transforms discrete, variable-radius, pressure- and tilt-modeled stroke centerlines into continuous, non-overlapping 2D closed polygon contours (`BLPath`) optimized for real-time subpixel anti-aliased rasterization via [Blend2D](https://blend2d.com).

Originally engineered for **FolioNote**, Folio-Sketch provides ultra-low latency, physically modeled stroke tessellation suitable for handwriting, note-taking, sketching, and digital calligraphy applications.

---

## Architecture & Directory Layout

```text
Folio-Sketch/
├── CMakeLists.txt                 # Modern CMake build system
├── LICENSE                        # GNU AGPLv3
├── README.md                      # Documentation
├── include/
│   └── folio_sketch/
│       ├── ink_generator.hpp      # Primary vector stroke & infill generator API
│       └── ink_stroke_data.hpp    # Geometric primitives (StrokePoint, CapType, InfillStyle)
├── src/
│   ├── ink_generator_internal.hpp # Shared internal math, nib geometries, and cap tracers
│   ├── ink_generator_caps.cpp     # Oriented nib profiles (Round, Square, Chisel) & endcaps
│   ├── ink_generator_ribbon.cpp   # Miter-clipped single contour ribbon tessellator
│   ├── ink_generator_stroke.cpp   # Open stroke builder & O(1) live inking incremental append
│   ├── ink_generator_polygon.cpp  # Closed shape loops & polygon outlines
│   ├── ink_generator_patterns.cpp # Arc-length line patterns (Dashed, Dotted) & Graphite pencil
│   └── ink_generator_infill.cpp   # Interior polygon infill & architectural drafting hatches
└── test/
    ├── CMakeLists.txt
    └── test_ink_generator.cpp     # Invariant validation suite & performance benchmarks
```

---

## Core Features

### 1. Miter-Clipped Single Contour Tessellation (`BuildSolidRibbon`)
* **Continuous Contour**: Emits a single continuous, non-overlapping polygon contour wrapping around the stroke centerline from head to tail with exact endcap closures.
* **Inner Corner Projection Safety**: Automatically blends between ideal angle bisector miters for gentle curves ($\le 60^\circ$), smooth 3-point circular fillets for sharp corners ($60^\circ$ to $150^\circ$, including $90^\circ$ corners), and zero-projection bevel joins for dense samples or short segment turnarounds.
* **$180^\circ$ Hairpin Turnaround Domes**: Evaluates directional turnaround reversals using outward elbow normals $\vec{u}_{elbow} = \frac{\vec{u}_{in} - \vec{u}_{out}}{|\vec{u}_{in} - \vec{u}_{out}|}$, ensuring turnaround domes always bow outward in the direction of pen motion without inverted loop spikes.

### 2. Multi-Geometry Oriented Nibs
* **Round Nib**: 32-vertex regular polygon circle for ballpoint pens, fineliners, and pencil tools.
* **Square Blade Nib**: Fixed $90^\circ$ rectangular blade ($0.9r \times 2.6r$) for realistic highlighter simulation with thin vertical strokes and thick horizontal bands.
* **Chisel Diamond Nib**: Fixed $45^\circ$ diamond blade for expressive calligraphy and fountain pen emulation.
* **Facet Perimeter Traversal**: Square and Chisel corner transitions smoothly traverse intermediate vertices along the nib perimeter, preventing interior cuts, double-fang spikes, and inverted V-notches.

### 3. Real-Time Incremental Append (`AppendRibbonSegments`)
* Generates live inking geometry in $O(1)$ time per frame as new hardware digitizer events arrive.
* Appends incremental segment capsules directly to the active `BLPath`, eliminating $O(N^2)$ stroke regeneration lag during active drawing.

### 4. Multi-Scale Textured Graphite Engine (`RenderPencilStroke`)
* Simulates physical pencil-on-paper contact using deterministic 2D toroidal value noise on a $128 \times 128$ periodic lattice.
* Three concentric pressure contact bands:
  - **Band 0 (Full contact patch)**: Sparse paper tooth grain with directional tilt spread.
  - **Band 2 ($p > 0.22$)**: Intermediate mid-tone graphite concentration.
  - **Band 3 ($p > 0.38$)**: Deep compressed graphite core.

### 5. Architectural Drafting Infill Hatches (`FillInfill`)
* Renders seamlessly tiling, high-resolution ($256 \times 256$) drafting hatch patterns for closed vector polygons:
  - 45° Parallel diagonal lines (`HatchDiagonal`)
  - Intersecting 45° grid (`HatchCross`)
  - Horizontal lines (`HatchHorizontal`)
  - Vertical lines (`HatchVertical`)
  - Fine circular stipple dots (`HatchDots`)
  - Solid, translucent wash, and linear/radial gradients

---

## Performance Benchmarks

Tested on standard modern desktop hardware ($1920 \times 1080$ surface, 120 FPS target budget $\le 8.33\text{ ms}$):

| Operation | Scale | Time |
| :--- | :--- | :--- |
| **Incremental Append** | 1,000 points stream | **~0.65 ms** total |
| **CPU Tessellation** | 1,000 points (Single Contour) | **~0.17 ms** |
| **CPU Tessellation** | 2,500 points (10-second stroke) | **~0.53 ms** |
| **Solid Rasterization** | 1,000 points path | **~0.45 ms** / frame |
| **Solid Rasterization** | 2,500 points path | **~0.67 ms** / frame |
| **Textured Pencil** | 1,000 points path | **~1.47 ms** / frame |
| **Textured Pencil** | 2,500 points path | **~3.46 ms** / frame |

---

## Building and Integrating

### Standalone Build
```bash
git clone https://github.com/3dWonderGuy/Folio-Sketch.git
cd Folio-Sketch
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Integration via CMake `FetchContent` or Submodule
```cmake
# Add Folio-Sketch subdirectory
add_subdirectory(third_party/Folio-Sketch)

# Link against the FolioSketch target
target_link_libraries(your_app PRIVATE FolioSketch::FolioSketch)
```

Include headers in your code:
```cpp
#include <folio_sketch/ink_generator.hpp>

// Tessellate an ink stroke into a Blend2D BLPath
std::vector<StrokePoint> points = ...;
BLPath strokePath = InkGenerator::GenerateStroke(points, CapType::Round, StrokePattern::Solid);
```

---

## License
Folio-Sketch is licensed under the [GNU Affero General Public License v3.0 (AGPLv3)](LICENSE).
