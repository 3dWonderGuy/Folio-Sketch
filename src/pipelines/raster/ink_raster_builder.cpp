/**
 * =========================================================================================
 * @file src/pipelines/raster/ink_raster_builder.cpp
 * @brief Procreate-Style Raster Dab / Stamp & Smudge Instance Builder Implementation
 * =========================================================================================
 */

#include "pipelines/raster/ink_raster.hpp"
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

/**
 * @brief Constructs a single RasterDabInstance with scatter, orientation, and pressure opacity.
 */
[[nodiscard]] RasterDabInstance MakeDab(
    float x, float y, float radius, float pressure,
    float tangentX, float tangentY, float tiltX, float tiltY,
    ColorRgba baseColor, const RasterBrushConfig& cfg, uint32_t dabIdx) noexcept
{
    const float safeR = std::max(radius, 0.01f);

    // 1. Perpendicular scatter jitter
    if (cfg.scatterRatio > 0.0f) {
        const float normalX = -tangentY;
        const float normalY =  tangentX;
        const float offset = SignedHash(dabIdx, cfg.randomSeed) * cfg.scatterRatio * safeR;
        x += normalX * offset;
        y += normalY * offset;
    }

    // 2. Orientation angle (tangent or stylus tilt + random angular jitter)
    float angleRad = 0.0f;
    if (cfg.alignToTilt && (tiltX != 0.0f || tiltY != 0.0f)) {
        angleRad = std::atan2(tiltY, tiltX);
    } else if (cfg.alignToTangent && (tangentX != 0.0f || tangentY != 0.0f)) {
        angleRad = std::atan2(tangentY, tangentX);
    }
    if (cfg.angleJitterRad > 0.0f) {
        angleRad += SignedHash(dabIdx, cfg.randomSeed ^ 0xA5A5A5A5u) * cfg.angleJitterRad;
    }

    // 3. Pressure-modulated opacity
    const float clampedPressure = std::clamp(pressure, 0.0f, 1.0f);
    const float pressureGain = std::clamp(cfg.opacityPressure, 0.0f, 1.0f);
    const float opacity = (1.0f - pressureGain) + pressureGain * clampedPressure;

    RasterDabInstance dab{};
    dab.center     = { x, y };
    dab.radius     = safeR;
    dab.angleRad   = angleRad;
    dab.colorRgba  = PackPremultipliedGpuColor(baseColor, opacity);
    dab.hardness   = std::clamp(cfg.hardness, 0.0f, 1.0f);
    dab.grainDepth = std::clamp(cfg.grainDepth, 0.0f, 1.0f);
    dab.smudgePull = std::clamp(cfg.smudgePull, 0.0f, 1.0f);
    return dab;
}

} // namespace

void RasterDabBuilder::BuildDabInstances(
    std::span<const StrokePoint> points,
    ColorRgba baseColor,
    const RasterBrushConfig& config,
    std::vector<RasterDabInstance>& outDabs)
{
    outDabs.clear();
    RasterWalkCursor cursor{};
    AppendDabInstances(points, baseColor, config, cursor, outDabs);
}

void RasterDabBuilder::AppendDabInstances(
    std::span<const StrokePoint> points,
    ColorRgba baseColor,
    const RasterBrushConfig& config,
    RasterWalkCursor& inoutCursor,
    std::vector<RasterDabInstance>& outDabs)
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
        outDabs.push_back(MakeDab(
            static_cast<float>(p0.x), static_cast<float>(p0.y), p0.r, p0.pressure,
            tx, ty, p0.tiltX, p0.tiltY, baseColor, config, inoutCursor.dabCounter++));

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

        float distAlongSeg = inoutCursor.distanceCarryMm;
        while (distAlongSeg <= segLen) {
            const float t = distAlongSeg / segLen;
            const float x        = static_cast<float>(pA.x) + t * dx;
            const float y        = static_cast<float>(pA.y) + t * dy;
            const float r        = pA.r + t * (pB.r - pA.r);
            const float pressure = pA.pressure + t * (pB.pressure - pA.pressure);
            const float tiltX    = pA.tiltX + t * (pB.tiltX - pA.tiltX);
            const float tiltY    = pA.tiltY + t * (pB.tiltY - pA.tiltY);

            outDabs.push_back(MakeDab(
                x, y, r, pressure, tx, ty, tiltX, tiltY,
                baseColor, config, inoutCursor.dabCounter++));

            const float stepMm = std::max(minStepMm, (r * 2.0f) * spacingRatio);
            distAlongSeg += stepMm;
        }

        inoutCursor.distanceCarryMm = distAlongSeg - segLen;
    }

    inoutCursor.nextSegmentIndex = segCount;
}

} // namespace Folio
