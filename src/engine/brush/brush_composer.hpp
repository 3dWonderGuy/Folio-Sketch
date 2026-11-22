#pragma once
/**
 * =========================================================================================
 * @file src/engine/brush/brush_composer.hpp
 * @brief Brush Layer Manager, Signal Router, and Preset Recipe Assembler
 * =========================================================================================
 *
 * Pure builder/assembler for multi-coat BrushRecipes:
 *   1. Coat/Layer Stack Management (`AddLayer` — BasePigment, UnderlayShadow, GrainTexture, Highlight)
 *   2. Input Telemetry Signal Routing (`BindSignal` — Pressure/Tilt/Velocity -> Width/Opacity/Color)
 *   3. Folio Stylus & Expressive Preset Factory (`CreateFolioPresets`, `GetPresetRecipe`)
 *
 * Signal waveform evaluation lives in `brush_modulator.hpp` (`ModulatorEvaluator`) and
 * GPU capsule emission lives in `ink_capsule.hpp` (`CapsuleBuilder::BuildRecipeInstances`).
 */

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "core/ink_stroke_data.hpp"
#include "engine/brush/brush_recipe.hpp"
#include "pipelines/capsule/ink_capsule.hpp"

namespace Folio {

class BrushComposer {
public:
    /**
     * @brief Appends a new coat layer to a BrushRecipe and returns it for fluent configuration.
     */
    static BrushCoatSpec& AddLayer(
        BrushRecipe& recipe,
        std::string name,
        CoatRole role = CoatRole::BasePigment,
        float widthScale = 1.0f
    );

    /**
     * @brief Configures the nib profile (Round, Oval, Square, Chisel, Custom) for a specific coat layer.
     */
    static BrushCoatSpec& SetLayerNib(
        BrushCoatSpec& coat,
        NibShape shape,
        float aspectRatio = 1.0f,
        float angleRad = 0.0f,
        bool followPath = false,
        float roundness = 1.0f,
        std::string customId = ""
    );

    /**
     * @brief Attaches a signal modulator binding onto an existing brush coat layer.
     */
    static BrushCoatSpec& BindSignal(
        BrushCoatSpec& coat,
        ModulatorSource source,
        ModulatorTarget target,
        ModulatorWaveform waveform = ModulatorWaveform::Linear,
        float bias = 0.0f,
        float amplitude = 1.0f,
        float frequency = 1.0f
    );

    /**
     * @brief Resolves any StrokeStyle into a concrete multi-coat BrushRecipe.
     */
    static BrushRecipe RecipeFromStyle(const StrokeStyle& style);

    /**
     * @brief Looks up a pre-tuned preset recipe by preset enum.
     */
    static BrushRecipe GetPresetRecipe(BrushPreset preset);

    /**
     * @brief Returns the built-in library of Folio brush recipes plus Standard Solid. In brush presets
     */
    static std::vector<BrushRecipe> CreateFolioPresets();

    static BrushRecipe CreateBallpoint();
    static BrushRecipe CreateMarker();
    static BrushRecipe CreateFountain();
    static BrushRecipe CreateHighlighter();
    static BrushRecipe CreatePencil();
    static BrushRecipe CreateCalligraphy();

    /**
     * @brief Delegates recipe synthesis to CapsuleBuilder for callers using BrushComposer.
     */
    static void BuildRecipeInstances(
        std::span<const StrokePoint> points,
        ColorRgba baseColor,
        float baseWidth,
        const BrushRecipe& recipe,
        double currentTimeSec,
        std::vector<CapsuleInstance>& outInstances,
        double strokeEndTimeSec = 0.0
    );

    // Delegates to ModulatorEvaluator for color/noise utilities
    static void RgbToHsl(ColorRgba rgb, float& hDeg, float& s, float& l) noexcept;
    static ColorRgba HslToRgb(float hDeg, float s, float l, uint8_t a = 255) noexcept;
    static float Noise1D(float x, uint32_t seed) noexcept;
    static float Hash1D(uint32_t index, uint32_t seed) noexcept;
};

} // namespace Folio
