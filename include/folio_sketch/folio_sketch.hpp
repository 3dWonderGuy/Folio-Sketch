#pragma once
/**
 * @file include/folio_sketch/folio_sketch.hpp
 * @brief Umbrella Header for Folio-Sketch Vector Inking Library
 */

// 1. Core Foundations & Data Types
#include "folio_sketch/core/ink_stroke_data.hpp"
#include "folio_sketch/core/ink_config.hpp"
#include "folio_sketch/core/stroke_collision.hpp"

// 2. Real-Time Inking & Physics Engine
#include "folio_sketch/engine/ink_engine.hpp"
#include "folio_sketch/engine/stroke_width_evaluator.hpp"
#include "folio_sketch/engine/stroke_post_processor.hpp"

// 3. Hardware GPU Drawing Pipeline
#include "folio_sketch/gpu/ink_mesh.hpp"

// 4. 2D Vector Export & Outline Paths
#include "folio_sketch/vector/ink_path.hpp"
#include "folio_sketch/vector/ink_generator.hpp"
