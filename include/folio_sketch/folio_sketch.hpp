#pragma once

// 1. Core Foundations & Data Types
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/core/ink_config.hpp"
#include "folio_sketch/core/stroke_collision.hpp"

// 2. Hardware GPU Pipeline
#include "folio_sketch/gpu/ink_mesh.hpp"

// 3. Central Orchestrator & Dynamics
#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_sketch/engine/stroke_width_evaluator.hpp"
#include "folio_sketch/engine/stroke_post_processor.hpp"

// 4. Geometry & Vector Export
#include "folio_sketch/geometry/shape_recognizer.hpp"
#include "folio_sketch/vector/ink_path.hpp"
#include "folio_sketch/vector/ink_generator.hpp"
