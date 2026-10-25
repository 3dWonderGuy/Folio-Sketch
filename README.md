# Folio-Sketch
*High-Performance Computational 2D Vector Inking Engine Library*

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()
[![Backend: ThorSVG](https://img.shields.io/badge/Backend-ThorSVG-blue.svg)](https://www.thorvg.org)

## Overview
**Folio-Sketch** is a standalone, hardware-accelerated 2D computational vector geometry and inking engine. It transforms discrete, variable-radius, pressure- and tilt-modeled stroke centerlines into GPU indexed triangle meshes (`InkMesh`) for real-time hardware rasterization and vector path contours (`InkPath`) with [ThorSVG](https://www.thorvg.org) integration for scalable export and rendering.

Originally engineered for **FolioNote**, Folio-Sketch provides ultra-low latency, physically modeled stroke tessellation suitable for handwriting, note-taking, sketching, and digital calligraphy applications.

---

## Architecture & Directory Layout

```text
Folio-Sketch/
├── CMakeLists.txt                 # Modern CMake build system (C++20, static library, tests)
├── LICENSE                        # GNU AGPLv3
├── README.md                      # Documentation
├── include/
│   └── folio_sketch/
│       ├── folio_sketch.hpp       # Master umbrella include
│       ├── ink_engine.hpp         # Central inking facade, Google Ink Modeler & predictive pipeline
│       ├── ink_config.hpp         # Spring physics parameters, noise filtering & pressure calibration
│       ├── ink_stroke_data.hpp    # Geometric primitives (StrokePoint, CapType, InfillStyle, InkVertex, InkMesh)
│       ├── ink_mesh.hpp           # High-performance GPU triangle-strip mesh builder
│       ├── stroke_collision.hpp   # Two-tier capsule broadphase/narrowphase & stroke slicer
│       ├── stroke_width_evaluator.hpp # Dynamic width, velocity thinning, and EMA smoothing
│       ├── stroke_post_processor.hpp  # Topological analysis, loop detection & curve simplification
│       └── ink_generator.hpp      # Primary vector stroke & infill generator API
├── src/
│   ├── engine/
│   │   └── ink_engine.cpp         # Inking lifecycle, predictive continuation & modeler bridge
│   ├── generator/
│   │   ├── ink_generator_internal.hpp # Internal cap tracers and debug state
│   │   ├── ink_generator_caps.cpp     # Oriented nib profiles & cap tracing
│   │   ├── ink_generator_ribbon.cpp   # Clean ribbon orchestrator & sub-ribbon assembler
│   │   ├── ink_generator_stroke.cpp   # Open stroke builder & live inking incremental append
│   │   ├── ink_generator_polygon.cpp  # Closed shape loops & polygon outlines
│   │   ├── ink_generator_patterns.cpp # Arc-length patterns (Dashed, Dotted) & Graphite pencil
│   │   ├── ink_generator_infill.cpp   # Interior polygon infill & drafting hatches
│   │   ├── ink_generator_debug.cpp    # Visual stroke debugger overlay & diagnostic dumper
│   │   ├── ink_ribbon_corner.hpp      # Corner join solver (miter clamp, arc fans, nib beveling)
│   │   └── ink_ribbon_corner.cpp
│   ├── geometry/
│   │   ├── ink_nib_geometry.hpp       # Nib profiles (Round, Square, Chisel) & ribbon support offsets
│   │   ├── ink_stroke_filter.hpp      # Point decimation, Lipschitz slope limiter & hairpin detection
│   │   ├── ink_stroke_filter.cpp
│   │   ├── ink_geometry_utils.hpp     # Segment computation, intersection math & swallowtail pruning
│   │   └── ink_geometry_utils.cpp
│   └── mesh/
│       └── ink_mesh_builder.cpp       # GPU quad ribbon and smooth corner fan tessellator
└── test/
    ├── CMakeLists.txt
    ├── repro114.inc               # Tight curvature crescent regression reproducer dataset
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

// Tessellate an ink stroke into an InkPath (with ThorSVG Shape conversion)
std::vector<StrokePoint> points = ...;
InkPath strokePath = InkGenerator::GenerateStroke(points, CapType::Round, StrokePattern::Solid);
auto thorvgShape = strokePath.toThorVGShape();
```

---

## License
Folio-Sketch is licensed under the [GNU Affero General Public License v3.0 (AGPLv3)](LICENSE).
