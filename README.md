# Folio-Sketch
*High-Performance Computational Universal 2D Vector Inking Engine Library*

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()
[![ThorSVG](https://img.shields.io/badge/Backend-ThorSVG-blue.svg)](https://www.thorvg.org)

## Overview
**Folio-Sketch** is a standalone, hardware-accelerated 2D computational vector geometry and inking engine. Whole idea behind this library is to bring a powerful all around inking library to allow any application to have inking engine and best inking feel. This library is build around bringing best possbile natural writing experiance to any device. 

It transforms discrete, variable-radius, pressure- and tilt-modeled stroke centerlines into GPU Signed Distance Field (SDF) capsule instances (`CapsuleInstance`) and triangle meshes (`InkMesh`)-(**depreciated, not recommened**) for real-time hardware rasterization, as well as vector path contours (`InkPath`) with [ThorSVG](https://www.thorvg.org) integration for scalable export and rendering.

**This application consist of 3 major blocks:**
* **Core** : The main library code, jusut pure C++ code taking care of sprin-mass system modeling, interpolation smoothign filtering tuning.
* **Backends**: This is sort of shell add on specificaly made and tuned to allow applications to archive sub 5ms inking, your application will have to pass or even route rendering through this shell (main culprit behind latency is buffer swap this shell allows ink to render into screen rigth up -> more in documentation)
* **InspectorV2.0**: is a built in application that once imported into any application can be safely deleted, it is a debugger as well as a demo tool showing what this libary can do. Point behind this application is to show how this library is intented to be setup for multiplatfrom use and use of backends


**From the Author:** I worked really hard to make this libray have best support for multiplatform applications it should provide equal experiance on any operating system (Android, IOS, Windows, Linux). I also tried to make its use as intuitive as possilbe so it should not take too long to connect it to your application. 
I could not find a good multiplatfrom inking engine and usualy I had to rely on some type of framework, I wanted something very fast and efficient and able to acess raw hardware power of device and push inking latency to sub 5ms directly competing and even beating some consumer availble applications in quality and feel. I decided to bring something new to community and took on challange to creat high perfofrmance inking engine from scratch with good help of modern available tools. I hope this would be good addition to opensource community. Also I want to say thank to developers behind Rnote, ++Xjounral and google developers who opensourced the Cahier app and underlying libraries. Thank you for taking interest in my library I hope you find it useful


---

## Documentation

Comprehensive design documents, mathematical proofs, and architectural details are maintained in the [`docs/`](docs/) directory:

- [**01. Core Architecture (Tier 1)**](docs/01_CORE_ARCHITECTURE.md): Pure headless C++20 math engine, 4-stage pipeline, Google Ink ODE solver, tremor filtering, and data boundaries.
- [**02. Backends & Hardware Scanout (Tier 2)**](docs/02_BACKENDS_AND_SCANOUT.md): Analytic capsule SDF shaders, persistent VBO streaming, subpixel AA, and Android front-buffer bypass.
- [**03. Inspector & Diagnostic Workbench Manual**](docs/03_INSPECTOR_GUIDE.md): Headless CI benchmarking (`--diagnose`), sliver tests, diagnostic tabs, and time-travel scrubber.
- [**04. Production Integration Guide**](docs/04_INTEGRATION_GUIDE.md): Turnkey application integration blueprint, wet-to-dry handoff lifecycle, and host bridge example.

---

## Core Features

### 1. High-Performance Physical Modeling & Vector Pipeline
* **Google Ink Modeler Integration**: Spring-mass-damper physics solver delivering the most natural, responsive pen feel.
* **Smart Anti-Wobble & Dynamics**: Built-in hardware jitter and wobble filters, hydrodynamic velocity thinning, corner compensation, and Lipschitz slope limits.
* **Kalman Prediction Tail**: Zero-perceived-latency continuation with distance-attenuated confidence fading.
* **Built-in Vector Export & Generation**: Direct conversion into clean vector paths (`InkPath`), single-contour ribbons (`BuildSolidRibbon`), and seamless [ThorSVG](https://www.thorvg.org) integration for scalable export (SVG/PDF).

### 2. Universal Multi-Coat Brush Engine (`BrushComposer`)
* **Layered Multi-Coat Styling**: Composes continuous ribbons, particle stamps, braided weaves, and musical notations in a unified pass.
* **Signal Modulators**: Maps physical and temporal signals (arc length, pressure, velocity, tilt, age since input) to visual properties (radius, offset, color, opacity) through waveforms and noise.
* **Expressive Built-in Presets**:
  - *Standard Pens*: Ballpoint, Marker, Fountain Pen, and non-darkening Highlighter (set-union blending).
  - *Natural Media*: Shading Pencil with procedural paper grain and tilt-responsive shading.
  - *Dynamic Inks*: Gravity-dripping Wet Paint, blinking Holiday Lights, undulating Jelly Wobble, braided Twisted Yarn, Flag Banner, Music Staff, and Graffiti Spray.

### 3. GPU Analytic Capsule SDF Renderer (`GlCapsuleRenderer`)
* **Sub-Millisecond Rasterization**: GPU-instanced Euclidean distance evaluation with sub-pixel antialiasing.
* **Gradient Color Transitions**: Smooth endpoint color interpolation eliminating stepping artifacts.
* **Procedural Paper Tooth**: Integrated micro-fiber grain shader for realistic graphite texture without texture atlases.
* **Artifact-Free Scanout**: Offscreen scratch composition for multi-coat/animated strokes delivered via a single scissored blit to eliminate frontbuffer flicker.

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


---

## License
Folio-Sketch is licensed under the [GNU Affero General Public License v3.0 (AGPLv3)](LICENSE).
