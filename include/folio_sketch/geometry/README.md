# FolioSketch Geometry Module

This directory provides geometric analysis and primitive recognition algorithms for strokes.

---

### 1. `shape_recognizer.hpp`
* **What it does:** Classifies hand-drawn strokes into clean, geometric primitive shapes and generates synthetic replacement geometry.
* **Key components:**
  - `ShapeRecognizer`: Analyzes corner counts, closure gaps, and aspect ratios from RDP-decimated stroke points.
  - Recognizes primitives: `Line`, `Circle`, `Ellipse`, `Triangle`, `Rectangle`, and `Square`.
  - Generates replacement `StrokePoint` sequences with fitted geometry while preserving the user's base stroke thickness.
