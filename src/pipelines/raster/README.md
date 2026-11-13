# Raster Dab & Smudge Pipeline (`src/pipelines/raster`)

This pipeline implements a Procreate-style stamp/dab emitter (`RasterDabBuilder`) that walks modeled stroke curves by arc length ($\Delta s = \text{spacingRatio} \times \text{diameter}$) and outputs tightly packed 32-byte `RasterDabInstance` descriptors for GPU FBO painting and canvas smudging.

---

### Files

* **`ink_raster.hpp`**:
  - `RasterDabInstance` (32-byte POD): Stamp center $(x, y)$, radius, rotation angle, premultiplied RGBA8 color, edge hardness, paper grain depth, and canvas smudge pull weight.
  - `RasterBrushConfig`: Controls tip silhouette (`RasterTipShape`), spacing ratio, perpendicular scatter, angular jitter, tangent/tilt alignment, grain intensity, and smudge pickup.
  - `RasterWalkCursor`: Carries sub-segment arc-length remainder across frames for seamless $O(1)$ live drawing.
* **`ink_raster_builder.cpp`**:
  - Implements full-stroke (`BuildDabInstances`) and incremental live-stroke (`AppendDabInstances`) dab generation.
