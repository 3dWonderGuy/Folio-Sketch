/**
 * =========================================================================================
 * @file src/pipelines/raster/ink_raster_builder.cpp
 * @brief Procreate-Style Raster Dab / Stamp & Paint Depletion Builder Implementation
 * =========================================================================================
 */

#include "pipelines/raster/ink_raster.hpp"
#include "engine/stylus_dynamics.hpp"
#include "engine/brush/brush_composer.hpp"
#include <algorithm>
#include <cmath>

namespace Folio {

namespace {

/**
 * @brief Fast deterministic 32-bit integer hash mapped to [-1.0f, +1.0f].
 */
[[nodiscard]] constexpr float SignedHash(uint32_t index, uint32_t seed) noexcept {
    uint32_t h = index * 0x9E3779B9u ^ (seed + 0x85EBCA6Bu);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    const float unit = static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
    return unit * 2.0f - 1.0f;
}

[[nodiscard]] const InkingConfig& GetDefaultTiltConfig() noexcept {
    static const InkingConfig kCfg = []() {
        InkingConfig c{};
        c.dynamics.useTilt = true;
        return c;
    }();
    return kCfg;
}

/**
 * @brief Constructs a single RasterDabInstance with scatter, tilt spread, depletion, and material grain.
 */
[[nodiscard]] RasterDabInstance MakeDab(
    float x, float y, float radius, float pressure,
    float tangentX, float tangentY, float tiltX, float tiltY,
    float speedMmPerSec, float remainingCharge,
    ColorRgba baseColor, const RasterBrushConfig& cfg,
    const CanvasMaterial& material, uint32_t dabIdx) noexcept
{
    const float safeR = std::max(radius, 0.01f);

    // 1. Evaluate stylus tilt magnitude & azimuth
    const StylusTiltSignal tiltSig = StylusDynamics::EvaluateTilt(tiltX, tiltY, GetDefaultTiltConfig());
    const float tiltScale = 1.0f + tiltSig.magnitude * std::max(0.0f, cfg.tiltSpreadMultiplier - 1.0f);
    float spreadR = safeR * tiltScale;
    if (cfg.sizeJitter > 0.0f) {
        const float sizeScale = 1.0f + SignedHash(dabIdx, cfg.randomSeed ^ 0x1B873593u) * std::clamp(cfg.sizeJitter, 0.0f, 0.9f);
        spreadR = std::max(0.05f, spreadR * sizeScale);
    }

    // 2. Perpendicular scatter jitter
    if (cfg.scatterRatio > 0.0f) {
        const float normalX = -tangentY;
        const float normalY =  tangentX;
        const float offset = SignedHash(dabIdx, cfg.randomSeed) * cfg.scatterRatio * spreadR;
        x += normalX * offset;
        y += normalY * offset;
    }

    // 3. Orientation angle (stylus tilt azimuth or stroke tangent + random angular jitter)
    float angleRad = 0.0f;
    if (cfg.alignToTilt && tiltSig.magnitude > 0.0f) {
        angleRad = tiltSig.azimuthRad;
    } else if (cfg.alignToTangent && (tangentX != 0.0f || tangentY != 0.0f)) {
        angleRad = std::atan2(tangentY, tangentX);
    }
    if (cfg.angleJitterRad > 0.0f) {
        angleRad += SignedHash(dabIdx, cfg.randomSeed ^ 0xA5A5A5A5u) * cfg.angleJitterRad;
    }

    // 4. Pressure, reservoir depletion, and speed starvation opacity
    const float clampedPressure = std::clamp(pressure, 0.0f, 1.0f);
    const float pressureGain = std::clamp(cfg.opacityPressure, 0.0f, 1.0f);
    const float pressureOpacity = (1.0f - pressureGain) + pressureGain * clampedPressure;
    const float chargeOpacity = std::lerp(
        std::clamp(cfg.minDepletedOpacity, 0.0f, 1.0f),
        1.0f,
        std::clamp(remainingCharge, 0.0f, 1.0f));
    const float speedStarvation = 1.0f - std::clamp(cfg.velocityThinning, 0.0f, 1.0f) *
        std::clamp(speedMmPerSec / 1500.0f, 0.0f, 1.0f);
    float opacity = std::clamp(pressureOpacity * chargeOpacity * speedStarvation, 0.0f, 1.0f);
    if (cfg.opacityJitter > 0.0f) {
        const float opNoise = 1.0f - std::abs(SignedHash(dabIdx, cfg.randomSeed ^ 0x85EBCA6Bu)) * std::clamp(cfg.opacityJitter, 0.0f, 0.95f);
        opacity = std::clamp(opacity * opNoise, 0.0f, 1.0f);
    }

    // 4b. Per-dab organic pigment hue / lightness variation
    ColorRgba dabColor = baseColor;
    if (cfg.hueJitterDeg > 0.0f || cfg.lightnessJitter > 0.0f) {
        float h = 0.0f, s = 0.0f, l = 0.0f;
        BrushComposer::RgbToHsl(baseColor, h, s, l);
        if (cfg.hueJitterDeg > 0.0f) {
            h += SignedHash(dabIdx, cfg.randomSeed ^ 0x27D4EB2Fu) * cfg.hueJitterDeg;
        }
        if (cfg.lightnessJitter > 0.0f) {
            l = std::clamp(l + SignedHash(dabIdx, cfg.randomSeed ^ 0x165667B1u) * cfg.lightnessJitter, 0.0f, 1.0f);
        }
        dabColor = BrushComposer::HslToRgb(h, s, l, baseColor.a);
    }

    // 5. Material grain depth: dry bristles & tilted pencil skim paper peaks more strongly
    const float baseGrain = std::max(cfg.grainDepth, material.toothDepth * 0.85f);
    float effectiveGrain = baseGrain + (1.0f - remainingCharge) * 0.35f + tiltSig.magnitude * 0.15f;
    effectiveGrain *= std::lerp(1.25f, 0.85f, clampedPressure);

    RasterDabInstance dab{};
    dab.center     = { x, y };
    dab.radius     = spreadR;
    dab.angleRad   = angleRad;
    dab.colorRgba  = PackPremultipliedGpuColor(dabColor, opacity);
    dab.hardness   = std::clamp(cfg.hardness, 0.0f, 1.0f);
    dab.grainDepth = std::clamp(effectiveGrain, 0.0f, 1.0f);
    dab.smudgePull = std::clamp(cfg.smudgePull, 0.0f, 1.0f);
    return dab;
}

[[nodiscard]] constexpr float ComputeRemainingCharge(
    float traveledDistanceMm,
    const RasterBrushConfig& config,
    const CanvasMaterial& material) noexcept
{
    if (config.chargeDistanceMm <= 0.0f) {
        return 1.0f;
    }
    const float dragFactor = 1.0f - 0.5f * std::clamp(material.surfaceDrag, 0.0f, 1.0f);
    const float effectiveDist = std::max(1.0f, config.chargeDistanceMm * dragFactor);
    return std::clamp(1.0f - (traveledDistanceMm / effectiveDist), 0.0f, 1.0f);
}

} // namespace

void RasterDabBuilder::BuildDabInstances(
    std::span<const StrokePoint> points,
    ColorRgba baseColor,
    const RasterBrushConfig& config,
    std::vector<RasterDabInstance>& outDabs,
    const CanvasMaterial& material)
{
    outDabs.clear();
    RasterWalkCursor cursor{};
    AppendDabInstances(points, baseColor, config, cursor, outDabs, material);
}

void RasterDabBuilder::AppendDabInstances(
    std::span<const StrokePoint> points,
    ColorRgba baseColor,
    const RasterBrushConfig& config,
    RasterWalkCursor& inoutCursor,
    std::vector<RasterDabInstance>& outDabs,
    const CanvasMaterial& material)
{
    const size_t n = points.size();
    if (n == 0) return;

    const float spacingRatio = std::max(config.spacingRatio, 0.01f);
    const float minStepMm    = std::max(config.minSpacingMm, 1e-3f);

    // Emit initial contact dab at the start of the stroke
    if (!inoutCursor.emittedFirstDab) {
        const StrokePoint& p0 = points[0];
        float tx = 1.0f;
        float ty = 0.0f;
        if (n > 1) {
            const float dx = static_cast<float>(points[1].x - p0.x);
            const float dy = static_cast<float>(points[1].y - p0.y);
            const float len = std::hypot(dx, dy);
            if (len > 1e-5f) {
                tx = dx / len;
                ty = dy / len;
            }
        }
        inoutCursor.remainingCharge = ComputeRemainingCharge(inoutCursor.traveledDistanceMm, config, material);
        outDabs.push_back(MakeDab(
            static_cast<float>(p0.x), static_cast<float>(p0.y), p0.r, p0.pressure,
            tx, ty, p0.tiltX, p0.tiltY, 0.0f, inoutCursor.remainingCharge,
            baseColor, config, material, inoutCursor.dabCounter++));

        const float diameter = std::max(p0.r * 2.0f, minStepMm);
        inoutCursor.distanceCarryMm = std::max(minStepMm, diameter * spacingRatio);
        inoutCursor.emittedFirstDab = true;
    }

    if (n < 2) return;

    const size_t segCount = n - 1;
    for (size_t i = inoutCursor.nextSegmentIndex; i < segCount; ++i) {
        const StrokePoint& pA = points[i];
        const StrokePoint& pB = points[i + 1];

        const float dx = static_cast<float>(pB.x - pA.x);
        const float dy = static_cast<float>(pB.y - pA.y);
        const float segLen = std::hypot(dx, dy);

        if (segLen < 1e-5f) {
            continue;
        }

        const float tx = dx / segLen;
        const float ty = dy / segLen;
        const double dtSec = pB.timestampSec - pA.timestampSec;
        const float segSpeedMmPerSec = (dtSec > 1e-5) ? (segLen / static_cast<float>(dtSec)) : 0.0f;

        const float baseTravel = inoutCursor.traveledDistanceMm;
        float distAlongSeg = inoutCursor.distanceCarryMm;
        while (distAlongSeg <= segLen) {
            const float t = distAlongSeg / segLen;
            const float x        = static_cast<float>(pA.x) + t * dx;
            const float y        = static_cast<float>(pA.y) + t * dy;
            const float r        = pA.r + t * (pB.r - pA.r);
            const float pressure = pA.pressure + t * (pB.pressure - pA.pressure);
            const float tiltX    = pA.tiltX + t * (pB.tiltX - pA.tiltX);
            const float tiltY    = pA.tiltY + t * (pB.tiltY - pA.tiltY);

            const float dabCharge = ComputeRemainingCharge(baseTravel + distAlongSeg, config, material);
            inoutCursor.remainingCharge = dabCharge;

            outDabs.push_back(MakeDab(
                x, y, r, pressure, tx, ty, tiltX, tiltY,
                segSpeedMmPerSec, dabCharge,
                baseColor, config, material, inoutCursor.dabCounter++));

            const float stepMm = std::max(minStepMm, (r * 2.0f) * spacingRatio);
            distAlongSeg += stepMm;
        }

        inoutCursor.traveledDistanceMm = baseTravel + segLen;
        inoutCursor.remainingCharge = ComputeRemainingCharge(inoutCursor.traveledDistanceMm, config, material);
        inoutCursor.distanceCarryMm = distAlongSeg - segLen;
    }

    inoutCursor.nextSegmentIndex = segCount;
}

RasterBrushConfig RasterDabBuilder::GetProBrushPreset(ProBrushPreset preset) noexcept {
    RasterBrushConfig cfg{};
    switch (preset) {
        case ProBrushPreset::HB_Pencil:
            cfg.tipShape             = RasterTipShape::ChalkSpeckle;
            cfg.spacingRatio         = 0.10f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.55f;
            cfg.aspectRatio          = 0.85f;
            cfg.scatterRatio         = 0.06f;
            cfg.angleJitterRad       = 0.4f;
            cfg.alignToTangent       = true;
            cfg.alignToTilt          = true;
            cfg.tiltSpreadMultiplier = 2.6f;
            cfg.opacityPressure      = 0.85f;
            cfg.grainDepth           = 0.65f;
            cfg.chargeDistanceMm     = 0.0f;
            cfg.smudgePull           = 0.0f;
            break;

        case ProBrushPreset::VineCharcoal:
            cfg.tipShape             = RasterTipShape::CharcoalGrain;
            cfg.spacingRatio         = 0.11f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.35f;
            cfg.aspectRatio          = 0.72f;
            cfg.scatterRatio         = 0.18f;
            cfg.angleJitterRad       = 1.2f;
            cfg.alignToTangent       = true;
            cfg.alignToTilt          = true;
            cfg.tiltSpreadMultiplier = 2.4f;
            cfg.opacityPressure      = 0.90f;
            cfg.grainDepth           = 0.82f;
            cfg.chargeDistanceMm     = 0.0f;
            cfg.smudgePull           = 0.10f;
            break;

        case ProBrushPreset::SoftPastel:
            cfg.tipShape             = RasterTipShape::ChalkSpeckle;
            cfg.spacingRatio         = 0.13f;
            cfg.minSpacingMm         = 0.04f;
            cfg.hardness             = 0.40f;
            cfg.aspectRatio          = 0.68f;
            cfg.scatterRatio         = 0.25f;
            cfg.angleJitterRad       = 1.8f;
            cfg.alignToTangent       = true;
            cfg.alignToTilt          = true;
            cfg.tiltSpreadMultiplier = 2.2f;
            cfg.opacityPressure      = 0.80f;
            cfg.grainDepth           = 0.88f;
            cfg.chargeDistanceMm     = 450.0f;
            cfg.minDepletedOpacity   = 0.15f;
            cfg.smudgePull           = 0.15f;
            break;

        case ProBrushPreset::OilFlatBristle:
            cfg.tipShape             = RasterTipShape::FlatBristle;
            cfg.spacingRatio         = 0.08f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.78f;
            cfg.aspectRatio          = 0.34f;
            cfg.scatterRatio         = 0.04f;
            cfg.angleJitterRad       = 0.08f;
            cfg.alignToTangent       = true;
            cfg.alignToTilt          = false;
            cfg.tiltSpreadMultiplier = 1.25f;
            cfg.opacityPressure      = 0.50f;
            cfg.grainDepth           = 0.45f;
            cfg.chargeDistanceMm     = 280.0f;
            cfg.minDepletedOpacity   = 0.12f;
            cfg.smudgePull           = 0.25f;
            break;

        case ProBrushPreset::AerosolSprayCan:
            cfg.tipShape             = RasterTipShape::RoundSoft;
            cfg.spacingRatio         = 0.20f;
            cfg.minSpacingMm         = 0.05f;
            cfg.hardness             = 0.05f;
            cfg.aspectRatio          = 1.0f;
            cfg.scatterRatio         = 0.85f;
            cfg.angleJitterRad       = 3.14159f;
            cfg.alignToTangent       = false;
            cfg.alignToTilt          = false;
            cfg.tiltSpreadMultiplier = 1.6f;
            cfg.opacityPressure      = 0.45f;
            cfg.grainDepth           = 0.25f;
            cfg.chargeDistanceMm     = 0.0f;
            cfg.velocityThinning     = 0.65f;
            cfg.smudgePull           = 0.0f;
            break;

        case ProBrushPreset::WetGouacheMixer:
            cfg.tipShape             = RasterTipShape::RoundHard;
            cfg.spacingRatio         = 0.08f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.82f;
            cfg.aspectRatio          = 0.88f;
            cfg.scatterRatio         = 0.02f;
            cfg.angleJitterRad       = 0.2f;
            cfg.alignToTangent       = true;
            cfg.alignToTilt          = false;
            cfg.tiltSpreadMultiplier = 1.3f;
            cfg.opacityPressure      = 0.72f;
            cfg.grainDepth           = 0.25f;
            cfg.chargeDistanceMm     = 420.0f;
            cfg.minDepletedOpacity   = 0.22f;
            cfg.smudgePull           = 0.65f;
            break;

        case ProBrushPreset::AcrylicImpasto:
            cfg.tipShape             = RasterTipShape::FlatBristle;
            cfg.spacingRatio         = 0.06f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.90f;
            cfg.aspectRatio          = 0.42f;
            cfg.scatterRatio         = 0.02f;
            cfg.angleJitterRad       = 0.05f;
            cfg.lightnessJitter      = 0.05f;
            cfg.alignToTangent       = true;
            cfg.opacityPressure      = 0.35f;
            cfg.grainDepth           = 0.30f;
            cfg.chargeDistanceMm     = 360.0f;
            cfg.minDepletedOpacity   = 0.20f;
            cfg.smudgePull           = 0.18f;
            break;

        case ProBrushPreset::WatercolorWash:
            cfg.tipShape             = RasterTipShape::RoundSoft;
            cfg.spacingRatio         = 0.14f;
            cfg.minSpacingMm         = 0.04f;
            cfg.hardness             = 0.35f;
            cfg.aspectRatio          = 0.92f;
            cfg.scatterRatio         = 0.08f;
            cfg.sizeJitter           = 0.18f;
            cfg.opacityJitter        = 0.15f;
            cfg.hueJitterDeg         = 4.0f;
            cfg.alignToTangent       = true;
            cfg.opacityPressure      = 0.75f;
            cfg.grainDepth           = 0.55f;
            cfg.wetEdge              = 0.85f;
            cfg.chargeDistanceMm     = 0.0f;
            cfg.smudgePull           = 0.30f;
            break;

        case ProBrushPreset::DryBrush:
            cfg.tipShape             = RasterTipShape::FlatBristle;
            cfg.spacingRatio         = 0.09f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.85f;
            cfg.aspectRatio          = 0.30f;
            cfg.scatterRatio         = 0.06f;
            cfg.opacityJitter        = 0.22f;
            cfg.alignToTangent       = true;
            cfg.opacityPressure      = 0.65f;
            cfg.grainDepth           = 0.92f;
            cfg.chargeDistanceMm     = 140.0f;
            cfg.minDepletedOpacity   = 0.05f;
            break;

        case ProBrushPreset::PaintSplatter:
            cfg.tipShape             = RasterTipShape::RoundHard;
            cfg.spacingRatio         = 0.35f;
            cfg.minSpacingMm         = 0.08f;
            cfg.hardness             = 0.88f;
            cfg.aspectRatio          = 0.75f;
            cfg.scatterRatio         = 1.15f;
            cfg.angleJitterRad       = 3.14159f;
            cfg.sizeJitter           = 0.75f;
            cfg.opacityJitter        = 0.35f;
            cfg.hueJitterDeg         = 10.0f;
            cfg.lightnessJitter      = 0.08f;
            cfg.alignToTangent       = false;
            cfg.opacityPressure      = 0.55f;
            cfg.grainDepth           = 0.15f;
            break;

        case ProBrushPreset::PaletteKnife:
            cfg.tipShape             = RasterTipShape::FlatBristle;
            cfg.spacingRatio         = 0.05f;
            cfg.minSpacingMm         = 0.03f;
            cfg.hardness             = 0.95f;
            cfg.aspectRatio          = 0.18f;
            cfg.scatterRatio         = 0.0f;
            cfg.angleJitterRad       = 0.02f;
            cfg.alignToTangent       = true;
            cfg.opacityPressure      = 0.30f;
            cfg.grainDepth           = 0.22f;
            cfg.chargeDistanceMm     = 220.0f;
            cfg.minDepletedOpacity   = 0.10f;
            cfg.smudgePull           = 0.85f;
            break;

        case ProBrushPreset::Custom:
        default:
            break;
    }
    return cfg;
}

const char* RasterDabBuilder::GetProBrushPresetName(ProBrushPreset preset) noexcept {
    switch (preset) {
        case ProBrushPreset::HB_Pencil:       return "HB Pencil";
        case ProBrushPreset::VineCharcoal:    return "Vine Charcoal";
        case ProBrushPreset::SoftPastel:      return "Soft Pastel";
        case ProBrushPreset::OilFlatBristle:  return "Oil Flat Bristle";
        case ProBrushPreset::AcrylicImpasto:  return "Acrylic Impasto";
        case ProBrushPreset::WatercolorWash:  return "Watercolor Wash";
        case ProBrushPreset::DryBrush:        return "Dry Brush";
        case ProBrushPreset::AerosolSprayCan: return "Aerosol Spray Can";
        case ProBrushPreset::PaintSplatter:   return "Paint Splatter";
        case ProBrushPreset::WetGouacheMixer: return "Wet Gouache Mixer";
        case ProBrushPreset::PaletteKnife:    return "Palette Knife";
        case ProBrushPreset::Custom:          return "Custom Pro Brush";
    }
    return "Custom Pro Brush";
}

} // namespace Folio
