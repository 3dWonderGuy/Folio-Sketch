#pragma once

// 1. Core Foundations & Data Types
#include "core/ink_stroke_data.hpp"
#include "core/stroke_presets.hpp"
#include "core/ink_config.hpp"
#include "core/stroke_collision.hpp"

// 2. Output Pipelines (Capsule, Vector, Raster)
#include "pipelines/capsule/ink_capsule.hpp"
#include "pipelines/vector/ink_path.hpp"
#include "pipelines/vector/ink_generator.hpp"
#include "pipelines/raster/ink_raster.hpp"

// 3. Central Orchestrator, Dynamics, Post-Processing & Brushes
#include "engine/ink_engine.hpp"
#include "engine/dynamics/stroke_width_evaluator.hpp"
#include "engine/post_processing/stroke_post_processor.hpp"
#include "engine/post_processing/shape_recognizer.hpp"
#include "engine/brush/brush_composer.hpp"
