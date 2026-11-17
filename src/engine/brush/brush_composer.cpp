/**
 * =========================================================================================
 * @file src/engine/brush/brush_composer.cpp
 * @brief Brush Layer Manager, Signal Router, and Assembler Implementation
 * =========================================================================================
 */

#include "engine/brush/brush_composer.hpp"
#include "engine/brush/brush_modulator.hpp"
#include <utility>

namespace Folio {

// Appends a new coat/layer to a recipe and returns a reference to configure it
BrushCoatSpec& BrushComposer::AddLayer(
    BrushRecipe& recipe,
    std::string name,
    CoatRole role,
    float widthScale
) {
    return recipe.AddCoat(std::move(name), role, widthScale);
}

// Configures the tip/nib shape for a specific coat layer
BrushCoatSpec& BrushComposer::SetLayerNib(
    BrushCoatSpec& coat,
    NibShape shape,
    float aspectRatio,
    float angleRad,
    bool followPath,
    float roundness,
    std::string customId
) {
    return coat.SetNib(shape, aspectRatio, angleRad, followPath, roundness, std::move(customId));
}

// Binds an input signal (pressure, tilt, speed) to a visual property (width, opacity, color)
BrushCoatSpec& BrushComposer::BindSignal(
    BrushCoatSpec& coat,
    ModulatorSource source,
    ModulatorTarget target,
    ModulatorWaveform waveform,
    float bias,
    float amplitude,
    float frequency
) {
    return coat.Bind(source, target, waveform, bias, amplitude, frequency);
}

// Builds a multi-coat recipe from high-level StrokeStyle flags (pressure, velocity, tilt)
BrushRecipe BrushComposer::RecipeFromStyle(const StrokeStyle& style) {
    BrushRecipe recipe = GetPresetRecipe(BrushPreset::BallpointPen);

    recipe.usePressure = style.usePressure;
    recipe.useVelocity = style.useVelocity;
    recipe.useTilt     = style.useTilt;

    recipe.EnsureBaseCoat();
    BrushCoatSpec& primary = recipe.coats.front();
    if (style.usePressure) {
        primary.BindPressureToWidth(0.2f, 1.5f);
    }
    if (style.useVelocity) {
        primary.BindVelocityToWidth(0.3f, 1500.0f);
    }
    if (style.useTilt) {
        primary.BindTiltToWidth(1.0f, 2.8f);
    }

    return recipe;
}

// Forwards multi-coat capsule generation to CapsuleBuilder
void BrushComposer::BuildRecipeInstances(
    std::span<const StrokePoint> points,
    ColorRgba baseColor,
    float baseWidth,
    const BrushRecipe& recipe,
    double currentTimeSec,
    std::vector<CapsuleInstance>& outInstances,
    double strokeEndTimeSec
) {
    CapsuleBuilder::BuildRecipeInstances(
        points,
        baseColor,
        baseWidth,
        recipe,
        currentTimeSec,
        outInstances,
        strokeEndTimeSec
    );
}

// Converts RGB color to HSL (hue degrees, saturation [0-1], lightness [0-1])
void BrushComposer::RgbToHsl(ColorRgba rgb, float& hDeg, float& s, float& l) noexcept {
    ModulatorEvaluator::RgbToHsl(rgb, hDeg, s, l);
}

// Converts HSL color back to RGB with specified alpha
ColorRgba BrushComposer::HslToRgb(float hDeg, float s, float l, uint8_t a) noexcept {
    return ModulatorEvaluator::HslToRgb(hDeg, s, l, a);
}

// 1D smooth cubic value noise for organic stroke variations
float BrushComposer::Noise1D(float x, uint32_t seed) noexcept {
    return ModulatorEvaluator::Noise1D(x, seed);
}

// Deterministic 1D integer-to-float pseudo-random hash in [0.0, 1.0]
float BrushComposer::Hash1D(uint32_t index, uint32_t seed) noexcept {
    return ModulatorEvaluator::Hash1D(index, seed);
}

} // namespace Folio
