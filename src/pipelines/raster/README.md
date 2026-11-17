# Raster Dab & Paint Simulation Pipeline (`src/pipelines/raster`)

This pipeline implements a Procreate-style stamp/dab emitter (`RasterDabBuilder`) that walks modeled stroke curves by arc length ($\Delta s = \text{spacingRatio} \times \text{diameter}$) and outputs tightly packed 32-byte `RasterDabInstance` descriptors for GPU FBO painting, bristle anisotropy, paint depletion, and canvas smudging.

---

### Files

* **`ink_raster.hpp`**:
  - `RasterDabInstance` (32-byte POD): Stamp center $(x, y)$, radius, rotation angle, premultiplied RGBA8 color, edge hardness, paper grain depth, and canvas smudge pull weight.
  - `ProBrushPreset`: 11 built-in pro media presets (`HB_Pencil`, `VineCharcoal`, `SoftPastel`, `OilFlatBristle`, `AcrylicImpasto`, `WatercolorWash`, `DryBrush`, `AerosolSprayCan`, `PaintSplatter`, `WetGouacheMixer`, `PaletteKnife`).
  - `RasterBrushConfig`: Controls tip silhouette (`RasterTipShape`), spacing ratio, anisotropy (`aspectRatio`), tilt spread (`tiltSpreadMultiplier`), paint reservoir charge (`chargeDistanceMm`, `minDepletedOpacity`), per-dab jitters (`sizeJitter`, `opacityJitter`, `hueJitterDeg`, `lightnessJitter`), watercolor `wetEdge`, and `smudgePull`.
  - `RasterWalkCursor`: Carries sub-segment arc-length remainder and `remainingCharge` across frames for seamless $O(1)$ live drawing.
* **`ink_raster_builder.cpp`**:
  - Implements full-stroke (`BuildDabInstances`) and incremental live-stroke (`AppendDabInstances`) dab generation coupled with `CanvasMaterial` tooth/drag and `StylusDynamics::EvaluateTilt`.
  - Implements `GetProBrushPreset` and `GetProBrushPresetName`.
