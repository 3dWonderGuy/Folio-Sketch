# Folio Brush Engine

A multi-coat, data-driven brush system inspired by Google Cahier. Brushes are pure recipes: they define layers, nib shapes, and dynamic signal bindings (pressure, tilt, speed) that map to visual outputs (width, opacity, color, offset).

---

## Core Concepts

A brush consists of three parts:

```text
┌────────────────────────────────────────────────────────────────────────┐
│                              BrushRecipe                               │
│  Global settings: opacity, paper grain depth, union overlap            │
│                                                                        │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                    Coat Layer 1 (e.g. Shadow)                    │  │
│  │  - Role & BlendMode    - Nib Shape (Round, Oval, Chisel, Square) │  │
│  │  - Offsets (X, Y)      - Signal Modulators                       │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                    Coat Layer 2 (e.g. Core Ink)                  │  │
│  │  - Role & BlendMode    - Nib Shape                               │  │
│  │  - Width Scale         - Signal Modulators                       │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
```

1. **`BrushRecipe`**: The container for the brush. Holds one or more coat layers.
2. **`BrushCoatSpec`**: A single layer in the stroke. Has its own **`BrushNib`** (tip shape), rendering role, blend mode, and modulators.
3. **`BrushModulator`**: Routes an input **Source** (pressure, tilt, speed) through a **Waveform** curve to a visual **Target** (width, opacity, color, offset).

---

## How to Create Brushes

### 1. Basic Single-Coat Pen (Ballpoint / Ink)
```cpp
#include "engine/brush/brush_composer.hpp"

Folio::BrushRecipe pen;
pen.name = "Ballpoint Pen";

pen.AddCoat("Ink", Folio::CoatRole::BasePigment, 0.40f)
   .SetRoundNib()
   .BindPressureToWidth(0.5f, 1.2f);
```

### 2. Highlighter (Chisel Nib + No Darkening Overlaps)
```cpp
Folio::BrushRecipe highlighter;
highlighter.name = "Highlighter";
highlighter.opacity = 0.38f;
highlighter.unionOverlap = true; // Overlapping lines will not darken

auto& coat = highlighter.AddCoat("Felt", Folio::CoatRole::BasePigment, 1.90f);
coat.SetChiselNib(/*aspectRatio=*/3.0f, /*angleRad=*/-0.785f /* -45 deg */);
coat.blendMode = Folio::CoatBlendMode::MaxUnion;
```

### 3. Multi-Coat Brush (Calligraphy with 3D Drop Shadow)
Each layer can use a completely different nib shape and offset:
```cpp
Folio::BrushRecipe brush;
brush.name = "3D Calligraphy";

// Layer 1: Darkened offset shadow underneath using a Round nib
auto& shadow = brush.AddUnderlayCoat("Shadow", /*widthScale=*/1.1f, 
                                     /*offsetX=*/0.4f, /*offsetY=*/0.6f, 
                                     /*opacity=*/0.5f, /*lightness=*/-0.3f);
shadow.SetRoundNib();
shadow.colorMode = Folio::CoatColorMode::InheritBase;

// Layer 2: Main stroke on top using a Chisel nib
auto& core = brush.AddCoat("Core", Folio::CoatRole::BasePigment, 1.0f);
core.SetChiselNib(/*aspectRatio=*/3.5f, /*angleRad=*/-0.785f);
core.BindDirectionToWidth(1.15f, 0.85f, -0.785f);
```

### 4. Shading Pencil (Tilt Widening + Paper Tooth Specks)
```cpp
Folio::BrushRecipe pencil;
pencil.name = "Shading Pencil";
pencil.useTilt = true;
pencil.SetTextureGrain(/*depth=*/0.85f, /*scale=*/2.4f);

// Layer 1: Graphite core (tilt widens, pressure darkens)
pencil.AddCoat("Body", Folio::CoatRole::BasePigment, 0.55f)
      .SetOvalNib(/*aspectRatio=*/1.8f)
      .BindPressureToOpacity(0.12f, 1.0f)
      .BindTiltToWidth(1.0f, 2.5f);

// Layer 2: Loose surface specks scattered on paper
pencil.AddParticleCoat("Specks", /*widthScale=*/0.10f, /*gapRatio=*/0.22f, /*lengthRatio=*/0.0f)
      .SetRoundNib()
      .Bind(Folio::ModulatorSource::ParticleIndex, Folio::ModulatorTarget::LateralOffset,
            Folio::ModulatorWaveform::HashRandom, -0.5f, 0.9f);
```

---

## Quick API Reference

### Nib Shapes (`SetNib`)
- `.SetRoundNib()`: Circular tip (ballpoint, standard pen).
- `.SetOvalNib(aspectRatio, angleRad, followPath)`: Elliptical tip.
- `.SetSquareNib(aspectRatio, angleRad, roundness)`: Rectangular blade.
- `.SetChiselNib(aspectRatio, angleRad)`: Angled diamond / calligraphy blade.
- `.SetCustomNib(customId, ...)`: Custom user-defined tip polygon or texture.

### Quick Signal Bindings
- `.BindPressureToWidth(min, max)`: Thicker when pressing harder.
- `.BindPressureToOpacity(min, max)`: Darker when pressing harder.
- `.BindTiltToWidth(min, max)`: Widens when pen is tilted flat (shading).
- `.BindVelocityToWidth(min, maxSpeed)`: Thins out at high speeds.
- `.BindDirectionToWidth(bias, amp, angle)`: Chisel nib angle thickness response.
- `.BindNoiseToWidth(min, max, freq)`: Organic rough edge jitter.

### Full Signal Matrix (`.Bind(...)`)
Connects any **Source** $\to$ **Waveform** $\to$ **Target**:
- **Sources**: `Pressure`, `Tilt`, `Velocity`, `TravelAngle`, `ArcLength`, `ParticleIndex`, `TimeSinceInput`.
- **Waveforms**: `Linear`, `SineWave`, `TriangleZigzag`, `MirrorCycle`, `AlternateSwap`, `ValueNoise1D`, `HashRandom`, `EaseOutClamped`.
- **Targets**: `RadiusScale`, `OpacityScale`, `LateralOffset`, `WorldOffsetX/Y`, `HueShiftDeg`, `LightnessShift`, `ParticleLength`.
