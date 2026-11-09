/**
 * =========================================================================================
 * @file src/engine/brush_composer.cpp
 * @brief Universal Multi-Coat & Signal-Modulator Inking Brush Composer Implementation
 * =========================================================================================
 */

#include "folio_sketch/engine/brush_composer.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct PointContext {
    float arcLength;
    float distanceRemaining;
    float pressure;
    float pressureIntegral;
    float velocity;
    float tilt;
    float travelAngle;
    float progress;
    float particleIndex;
    float timeSeconds;
    float timeSinceInput;
    float timeSinceStrokeEnd;
    float baseWidth;
};

float GetSourceValue(ModulatorSource src, const PointContext& ctx) noexcept {
    switch (src) {
        case ModulatorSource::ArcLength:          return ctx.arcLength;
        case ModulatorSource::DistanceRemaining:  return ctx.distanceRemaining;
        case ModulatorSource::Pressure:           return ctx.pressure;
        case ModulatorSource::PressureIntegral:   return ctx.pressureIntegral;
        case ModulatorSource::Velocity:           return ctx.velocity;
        case ModulatorSource::Tilt:               return ctx.tilt;
        case ModulatorSource::TravelAngle:        return ctx.travelAngle;
        case ModulatorSource::StrokeProgress:     return ctx.progress;
        case ModulatorSource::ParticleIndex:      return ctx.particleIndex;
        case ModulatorSource::TimeSeconds:        return ctx.timeSeconds;
        case ModulatorSource::TimeSinceInput:     return ctx.timeSinceInput;
        case ModulatorSource::TimeSinceStrokeEnd: return ctx.timeSinceStrokeEnd;
    }
    return 0.0f;
}

float EvaluateWaveform(ModulatorWaveform wf, float x, float rawLinearArg, const PointContext& ctx, uint32_t seed) noexcept {
    switch (wf) {
        case ModulatorWaveform::Linear:
            return x;

        case ModulatorWaveform::SineWave:
            return std::sin(x);

        case ModulatorWaveform::TriangleZigzag: {
            float norm = std::fmod(x / kTwoPi, 1.0f);
            if (norm < 0.0f) norm += 1.0f;
            return (norm < 0.5f) ? (4.0f * norm - 1.0f) : (3.0f - 4.0f * norm);
        }

        case ModulatorWaveform::MirrorCycle: {
            float frac = std::fmod(x / kTwoPi, 2.0f);
            if (frac < 0.0f) frac += 2.0f;
            return (frac <= 1.0f) ? frac : (2.0f - frac);
        }

        case ModulatorWaveform::AlternateSwap: {
            float norm = std::fmod(x / kTwoPi, 1.0f);
            if (norm < 0.0f) norm += 1.0f;
            return (norm < 0.5f) ? 1.0f : -1.0f;
        }

        case ModulatorWaveform::ValueNoise1D:
            return BrushComposer::Noise1D(x / kTwoPi, seed);

        case ModulatorWaveform::HashRandom:
            return BrushComposer::Hash1D(static_cast<uint32_t>(std::abs(rawLinearArg * 100.0f)), seed);

        case ModulatorWaveform::EaseOutClamped: {
            float u = std::clamp(rawLinearArg, 0.0f, 1.0f);
            float inv = 1.0f - u;
            return 1.0f - inv * inv * inv; // Cubic ease-out, stops at u=1.0
        }

        case ModulatorWaveform::EaseOutDripSynced: {
            // Google wet_paint: synchronized (EaseOut(timeSinceInput / 1.0s) * dripNoisePeak(arcLength, seed))
            float u = std::clamp(rawLinearArg, 0.0f, 1.0f);
            float inv = 1.0f - u;
            float easeOut = 1.0f - inv * inv * inv;

            // Spatial drip clustering along stroke (only distinct peaks become hanging drops)
            float bw = std::max(ctx.baseWidth, 1.0f);
            float noiseCoord = ctx.arcLength / (bw * 1.45f);
            float rawNoise = BrushComposer::Noise1D(noiseCoord, seed) * 0.5f + 0.5f; // [0, 1]
            float peak = std::max(0.0f, (rawNoise - 0.36f) / 0.64f);
            return easeOut * (peak * peak);
        }

        case ModulatorWaveform::BlinkPulseClamped: {
            // Google holiday_lights: bulb flashes over [0, 0.60s] when born, plus live multi-bulb twinkle while drawing
            float u = rawLinearArg; // timeSinceInput / 0.60s
            if (u < 1.0f && u >= 0.0f) {
                // Initial double flash as bulb is drawn (matches Google's 5-point piecewise linear flash curve)
                float flash = std::sin(u * kPi * 3.0f);
                return std::max(0.0f, flash);
            }
            // While stroke is still actively being drawn (timeSinceStrokeEnd == 0), bulbs twinkle at staggered phases
            if (ctx.timeSinceStrokeEnd <= 0.0f) {
                float twinkle = std::sin(ctx.timeSinceInput * 5.0f + ctx.particleIndex * 2.1f);
                return (twinkle > 0.25f) ? 0.85f : 0.0f;
            }
            return 0.0f;
        }

        case ModulatorWaveform::TravelingWaveJelly: {
            // Google jelly_wobble: wave along distanceRemaining + live phase that decelerates via EaseOut over 0.90s after pen lift
            float bw = std::max(ctx.baseWidth, 1.0f);
            float distCycles = ctx.distanceRemaining / (bw * 1.5f);
            float activePhaseTime = ctx.timeSinceInput;
            if (ctx.timeSinceStrokeEnd > 0.0f) {
                float uEnd = std::clamp(ctx.timeSinceStrokeEnd / 0.90f, 0.0f, 1.0f);
                float inv = 1.0f - uEnd;
                float easeEnd = 1.0f - inv * inv * inv;
                float ageAtPenUp = std::max(0.0f, ctx.timeSinceInput - ctx.timeSinceStrokeEnd);
                activePhaseTime = ageAtPenUp + easeEnd * 0.90f;
            }
            return std::sin((distCycles - activePhaseTime * 2.4f) * kTwoPi);
        }
    }
    return 0.0f;
}

struct EvaluatedMods {
    float radiusScale    = 1.0f;
    float lateralOffset  = 0.0f;
    float worldOffsetX   = 0.0f;
    float worldOffsetY   = 0.0f;
    float particleLength = 1.0f;
    float hueShiftDeg    = 0.0f;
    float lightnessShift = 0.0f;
    float opacityScale   = 1.0f;
};

EvaluatedMods EvaluateCoatModulators(const BrushCoatSpec& coat, const PointContext& ctx, float baseWidth) noexcept {
    EvaluatedMods res;
    res.lateralOffset = coat.lateralOffsetMm + coat.lateralOffsetRatio * baseWidth;
    if (coat.alternateLateralByParticle && (static_cast<uint32_t>(ctx.particleIndex) % 2 != 0)) {
        res.lateralOffset = -res.lateralOffset;
    }
    res.worldOffsetX  = coat.worldOffsetX + coat.worldOffsetXRatio * baseWidth;
    res.worldOffsetY  = coat.worldOffsetY + coat.worldOffsetYRatio * baseWidth;
    res.hueShiftDeg   = coat.hueOffsetDeg;
    res.lightnessShift= coat.lightnessOffset;
    res.opacityScale  = coat.opacityScale;

    for (const auto& mod : coat.modulators) {
        float srcVal = GetSourceValue(mod.source, ctx);
        float effFreq = (mod.periodInBrushSizes > 0.0f) ? (1.0f / (mod.periodInBrushSizes * std::max(baseWidth, 0.1f))) : mod.frequency;
        float rawLinearArg = effFreq * srcVal;
        float phaseArg = rawLinearArg * kTwoPi + mod.phase;
        float wave = EvaluateWaveform(mod.waveform, phaseArg, rawLinearArg, ctx, mod.seed);
        float effAmp = (mod.amplitudeRatio != 0.0f) ? (mod.amplitudeRatio * baseWidth) : mod.amplitude;
        float outVal = mod.bias + effAmp * wave;

        switch (mod.target) {
            case ModulatorTarget::RadiusScale:
                res.radiusScale = std::max(0.01f, res.radiusScale * outVal);
                break;
            case ModulatorTarget::LateralOffset:
                res.lateralOffset += outVal;
                break;
            case ModulatorTarget::WorldOffsetX:
                res.worldOffsetX += outVal;
                break;
            case ModulatorTarget::WorldOffsetY:
                res.worldOffsetY += outVal;
                break;
            case ModulatorTarget::ParticleLength:
                res.particleLength = std::max(0.05f, res.particleLength * outVal);
                break;
            case ModulatorTarget::HueShiftDeg:
                res.hueShiftDeg += outVal;
                break;
            case ModulatorTarget::LightnessShift:
                res.lightnessShift = std::clamp(res.lightnessShift + outVal, -1.0f, 1.0f);
                break;
            case ModulatorTarget::OpacityScale:
                res.opacityScale = std::clamp(res.opacityScale * outVal, 0.0f, 1.0f);
                break;
        }
    }
    return res;
}

ColorRgba ComputeCoatColor(const BrushCoatSpec& coat, ColorRgba baseColor, float hueShift, float lightnessShift, float& outAlpha) noexcept {
    ColorRgba c = baseColor;
    if (coat.colorMode == CoatColorMode::FixedColor) {
        c = coat.fixedColor;
    } else if (coat.colorMode == CoatColorMode::InvertBase) {
        hueShift += 180.0f;
    }

    if (std::abs(hueShift) > 0.1f || std::abs(lightnessShift) > 0.01f) {
        float h = 0.0f, s = 0.0f, l = 0.0f;
        BrushComposer::RgbToHsl(c, h, s, l);
        h = std::fmod(h + hueShift, 360.0f);
        if (h < 0.0f) h += 360.0f;
        l = std::clamp(l + lightnessShift, 0.0f, 1.0f);
        c = BrushComposer::HslToRgb(h, s, l, c.a);
    }

    outAlpha = (static_cast<float>(c.a) / 255.0f);
    return c;
}

} // namespace

void BrushComposer::RgbToHsl(ColorRgba rgb, float& hDeg, float& s, float& l) noexcept {
    float r = static_cast<float>(rgb.r) / 255.0f;
    float g = static_cast<float>(rgb.g) / 255.0f;
    float b = static_cast<float>(rgb.b) / 255.0f;

    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    l = (maxVal + minVal) * 0.5f;

    if (delta < 1e-5f) {
        hDeg = 0.0f;
        s = 0.0f;
        return;
    }

    s = (l > 0.5f) ? (delta / (2.0f - maxVal - minVal)) : (delta / (maxVal + minVal));

    if (maxVal == r) {
        hDeg = 60.0f * (std::fmod((g - b) / delta, 6.0f));
    } else if (maxVal == g) {
        hDeg = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        hDeg = 60.0f * (((r - g) / delta) + 4.0f);
    }

    if (hDeg < 0.0f) hDeg += 360.0f;
}

ColorRgba BrushComposer::HslToRgb(float hDeg, float s, float l, uint8_t a) noexcept {
    hDeg = std::fmod(hDeg, 360.0f);
    if (hDeg < 0.0f) hDeg += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    l = std::clamp(l, 0.0f, 1.0f);

    float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::abs(std::fmod(hDeg / 60.0f, 2.0f) - 1.0f));
    float m = l - c * 0.5f;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hDeg < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (hDeg < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (hDeg < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (hDeg < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (hDeg < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }

    return ColorRgba(
        static_cast<uint8_t>(std::clamp((r + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((g + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((b + m) * 255.0f, 0.0f, 255.0f)),
        a
    );
}

float BrushComposer::Hash1D(uint32_t index, uint32_t seed) noexcept {
    uint32_t h = index * 0x85ebca6bu ^ seed;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

float BrushComposer::Noise1D(float x, uint32_t seed) noexcept {
    int i = static_cast<int>(std::floor(x));
    float f = x - static_cast<float>(i);
    float u = f * f * (3.0f - 2.0f * f); // Hermite cubic curve

    float h0 = Hash1D(static_cast<uint32_t>(i), seed) * 2.0f - 1.0f;
    float h1 = Hash1D(static_cast<uint32_t>(i + 1), seed) * 2.0f - 1.0f;
    return (1.0f - u) * h0 + u * h1;
}

void BrushComposer::BuildRecipeInstances(
    std::span<const StrokePoint> points,
    ColorRgba baseColor,
    float baseWidth,
    const BrushRecipe& recipe,
    double currentTimeSec,
    std::vector<CapsuleInstance>& outInstances,
    double strokeEndTimeSec
) {
    outInstances.clear();
    if (points.size() < 2 || recipe.coats.empty()) {
        return;
    }

    const size_t N = points.size();

    // 1. Arc length, normal, and telemetry pre-pass
    std::vector<float> arcLengths(N, 0.0f);
    std::vector<float> pIntegrals(N, 0.0f);
    std::vector<CapsuleVec2> normals(N);
    std::vector<CapsuleVec2> tangents(N);

    float totalLen = 0.0f;
    float curPIntegral = 0.0f;

    for (size_t i = 1; i < N; ++i) {
        float dx = static_cast<float>(points[i].x - points[i - 1].x);
        float dy = static_cast<float>(points[i].y - points[i - 1].y);
        float segLen = std::sqrt(dx * dx + dy * dy);
        totalLen += segLen;
        arcLengths[i] = totalLen;

        float avgP = (points[i].pressure + points[i - 1].pressure) * 0.5f;
        curPIntegral += avgP * segLen;
        pIntegrals[i] = curPIntegral;

        if (segLen > 1e-5f) {
            tangents[i - 1] = { dx / segLen, dy / segLen };
            normals[i - 1]  = { -dy / segLen, dx / segLen };
        } else {
            tangents[i - 1] = { 1.0f, 0.0f };
            normals[i - 1]  = { 0.0f, 1.0f };
        }
    }
    tangents[N - 1] = tangents[N - 2];
    normals[N - 1]  = normals[N - 2];

    const float invTotalLen = (totalLen > 1e-4f) ? (1.0f / totalLen) : 0.0f;
    const float timeSeconds = static_cast<float>(currentTimeSec);
    const float tEnd = (strokeEndTimeSec > 0.0 && currentTimeSec >= strokeEndTimeSec)
        ? static_cast<float>(currentTimeSec - strokeEndTimeSec) : 0.0f;
    auto ageOf = [&](double ts) -> float {
        return (ts > 0.0 && currentTimeSec >= ts) ? static_cast<float>(currentTimeSec - ts) : 10.0f;
    };

    // Estimate capacity: continuous ribbons need (N-1) per coat, particles need approx (totalLen / gap)
    size_t approxCount = 0;
    for (const auto& coat : recipe.coats) {
        if (coat.emissionMode == CoatEmissionMode::ContinuousRibbon) {
            approxCount += (N - 1);
        } else {
            float gap = (coat.particleGapRatio > 0.0f) ? (coat.particleGapRatio * baseWidth) : coat.particleGapMm;
            gap = std::max(gap, 0.4f);
            approxCount += static_cast<size_t>(totalLen / gap + 2);
        }
    }
    outInstances.reserve(approxCount);

    // 2. Synthesize each coat sequentially
    for (const auto& coat : recipe.coats) {
        if (coat.emissionMode == CoatEmissionMode::ContinuousRibbon) {
            for (size_t i = 0; i < N - 1; ++i) {
                const auto& ptA = points[i];
                const auto& ptB = points[i + 1];

                PointContext ctxA{
                    arcLengths[i], totalLen - arcLengths[i], ptA.pressure, pIntegrals[i], 0.0f,
                    std::sqrt(ptA.tiltX * ptA.tiltX + ptA.tiltY * ptA.tiltY),
                    std::atan2(tangents[i].y, tangents[i].x),
                    arcLengths[i] * invTotalLen,
                    0.0f, timeSeconds, ageOf(ptA.timestampSec), tEnd, baseWidth
                };
                PointContext ctxB{
                    arcLengths[i + 1], totalLen - arcLengths[i + 1], ptB.pressure, pIntegrals[i + 1], 0.0f,
                    std::sqrt(ptB.tiltX * ptB.tiltX + ptB.tiltY * ptB.tiltY),
                    std::atan2(tangents[i + 1].y, tangents[i + 1].x),
                    arcLengths[i + 1] * invTotalLen,
                    0.0f, timeSeconds, ageOf(ptB.timestampSec), tEnd, baseWidth
                };

                EvaluatedMods modA = EvaluateCoatModulators(coat, ctxA, baseWidth);
                EvaluatedMods modB = EvaluateCoatModulators(coat, ctxB, baseWidth);

                float rBaseA = (ptA.r > 0.01f) ? ptA.r : (baseWidth * 0.5f);
                float rBaseB = (ptB.r > 0.01f) ? ptB.r : (baseWidth * 0.5f);

                float rA = rBaseA * coat.widthScale * modA.radiusScale;
                float rB = rBaseB * coat.widthScale * modB.radiusScale;

                CapsuleVec2 posA{
                    static_cast<float>(ptA.x) + normals[i].x * modA.lateralOffset + modA.worldOffsetX,
                    static_cast<float>(ptA.y) + normals[i].y * modA.lateralOffset + modA.worldOffsetY
                };
                CapsuleVec2 posB{
                    static_cast<float>(ptB.x) + normals[i + 1].x * modB.lateralOffset + modB.worldOffsetX,
                    static_cast<float>(ptB.y) + normals[i + 1].y * modB.lateralOffset + modB.worldOffsetY
                };

                float avgAlpha = 1.0f;
                ColorRgba col = ComputeCoatColor(
                    coat, baseColor,
                    (modA.hueShiftDeg + modB.hueShiftDeg) * 0.5f,
                    (modA.lightnessShift + modB.lightnessShift) * 0.5f,
                    avgAlpha
                );
                float finalAlpha = avgAlpha * ((modA.opacityScale + modB.opacityScale) * 0.5f);

                CapsuleInstance inst{};
                inst.pointA = posA;
                inst.pointB = posB;
                inst.radii = { rA, rB };
                inst.colorRgba = PackPremultipliedGpuColor(col, finalAlpha);
                inst.alphaFade = 1.0f;

                outInstances.push_back(inst);
            }
        } else {
            // Particle Stamp Mode
            const float gapBase = (coat.particleGapRatio > 0.0f) ? (coat.particleGapRatio * baseWidth) : coat.particleGapMm;
            const float gap = std::max(gapBase, 0.4f);
            size_t segIdx = 0;
            uint32_t pIdx = 0;

            for (float s = 0.5f * gap; s < totalLen; s += gap, ++pIdx) {
                if (s < coat.startSkipBrushSizes * baseWidth) continue;
                if (totalLen - s < coat.endSkipBrushSizes * baseWidth) continue;
                while (segIdx + 1 < N && arcLengths[segIdx + 1] < s) {
                    ++segIdx;
                }
                if (segIdx + 1 >= N) break;

                float segStartS = arcLengths[segIdx];
                float segEndS   = arcLengths[segIdx + 1];
                float segLen    = segEndS - segStartS;
                float t = (segLen > 1e-5f) ? ((s - segStartS) / segLen) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);

                const auto& ptA = points[segIdx];
                const auto& ptB = points[segIdx + 1];

                float px = static_cast<float>(ptA.x + (ptB.x - ptA.x) * t);
                float py = static_cast<float>(ptA.y + (ptB.y - ptA.y) * t);
                float pr = ptA.r + (ptB.r - ptA.r) * t;
                float pp = ptA.pressure + (ptB.pressure - ptA.pressure) * t;
                float pInteg = pIntegrals[segIdx] + (pIntegrals[segIdx + 1] - pIntegrals[segIdx]) * t;

                CapsuleVec2 tan{
                    tangents[segIdx].x + (tangents[segIdx + 1].x - tangents[segIdx].x) * t,
                    tangents[segIdx].y + (tangents[segIdx + 1].y - tangents[segIdx].y) * t
                };
                CapsuleVec2 norm{ -tan.y, tan.x };

                PointContext ctx{
                    s, totalLen - s, pp, pInteg, 0.0f, 0.0f,
                    std::atan2(tan.y, tan.x),
                    s * invTotalLen,
                    static_cast<float>(pIdx),
                    timeSeconds,
                    ageOf(ptA.timestampSec + (ptB.timestampSec - ptA.timestampSec) * t),
                    tEnd, baseWidth
                };

                EvaluatedMods mod = EvaluateCoatModulators(coat, ctx, baseWidth);

                float rStart = ((pr > 0.01f) ? pr : (baseWidth * 0.5f)) * coat.widthScale * mod.radiusScale;
                float rEnd   = rStart * coat.radiusEndRatio;

                CapsuleVec2 center{
                    px + norm.x * mod.lateralOffset + mod.worldOffsetX,
                    py + norm.y * mod.lateralOffset + mod.worldOffsetY
                };

                CapsuleVec2 dir{ 0.0f, 1.0f };
                switch (coat.particleOrientation) {
                    case ParticleOrientation::FollowTangent:
                        dir = tan;
                        break;
                    case ParticleOrientation::PerpendicularAlternate:
                        dir = (pIdx % 2 == 0) ? norm : CapsuleVec2{ -norm.x, -norm.y };
                        break;
                    case ParticleOrientation::GravityDown:
                        dir = { 0.0f, 1.0f }; // Always down
                        break;
                    case ParticleOrientation::FixedAngle:
                        dir = { 0.7071f, 0.7071f };
                        break;
                }

                if (coat.particleAngleRad != 0.0f) {
                    float cA = std::cos(coat.particleAngleRad);
                    float sA = std::sin(coat.particleAngleRad);
                    dir = { dir.x * cA - dir.y * sA, dir.x * sA + dir.y * cA };
                }

                float lenBase = (coat.particleLengthRatio > 0.0f) ? (coat.particleLengthRatio * baseWidth) : coat.particleLengthMm;
                float pLen = std::max(lenBase * mod.particleLength, 0.1f);
                CapsuleVec2 startPos = center;
                CapsuleVec2 endPos{ center.x + dir.x * pLen, center.y + dir.y * pLen };

                float alpha = 1.0f;
                ColorRgba col = ComputeCoatColor(coat, baseColor, mod.hueShiftDeg, mod.lightnessShift, alpha);
                float finalAlpha = alpha * mod.opacityScale;

                CapsuleInstance inst{};
                inst.pointA = startPos;
                inst.pointB = endPos;
                inst.radii = { rStart, rEnd };
                inst.colorRgba = PackPremultipliedGpuColor(col, finalAlpha);
                inst.alphaFade = 1.0f;

                outInstances.push_back(inst);
            }
        }
    }
}

std::vector<BrushRecipe> BrushComposer::CreateCahierPresets() {
    std::vector<BrushRecipe> presets;
    presets.reserve(13);

    // =========================================================================
    // 0. Standard Solid (Baseline)
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Standard Solid";
        r.description = "Baseline single-coat smooth capsule ink.";
        BrushCoatSpec coat;
        coat.name = "Solid Core";
        coat.widthScale = 1.0f;
        r.coats.push_back(coat);
        presets.push_back(r);
    }

    // =========================================================================
    // 1. Graffiti Spray (cahier: graffiti.brushfamily)
    // Exact Google specification: 3 coats, 20-deg slanted tip, outer halo + outline + rainbow core
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Graffiti Spray";
        r.description = "Google Graffiti: 3-coat street art brush with outer spray halo, dark border, and rainbow core.";

        // Coat 0: Outer Spray Halo (scale_x=1.15, scale_y=1.40, noise size 0.95..1.05, seed=3)
        BrushCoatSpec c0;
        c0.name = "Outer Spray Halo";
        c0.widthScale = 1.30f;
        c0.opacityScale = 0.40f;
        c0.colorMode = CoatColorMode::InheritBase;
        c0.modulators.push_back({
            ModulatorSource::ArcLength, ModulatorWaveform::ValueNoise1D,
            ModulatorTarget::RadiusScale, 0.20f, 0.0f, 0.0f, 0.05f, 0.0f, 1.0f, 3u
        });
        r.coats.push_back(c0);

        // Coat 1: Dark Outline Backer (scale_x=0.90, scale_y=1.22, fixed black, noise seed=1)
        BrushCoatSpec c1;
        c1.name = "Dark Outline Backer";
        c1.widthScale = 1.05f;
        c1.colorMode = CoatColorMode::FixedColor;
        c1.fixedColor = ColorRgba(15, 15, 20, 255);
        c1.modulators.push_back({
            ModulatorSource::ArcLength, ModulatorWaveform::ValueNoise1D,
            ModulatorTarget::RadiusScale, 0.20f, 0.0f, 0.0f, 0.08f, 0.0f, 1.0f, 1u
        });
        r.coats.push_back(c1);

        // Coat 2: Inner Core with Mirrored Hue Cycle (scale_x=0.45, scale_y=0.60, period=10*baseWidth)
        BrushCoatSpec c2;
        c2.name = "Rainbow Vibrant Core";
        c2.widthScale = 0.55f;
        c2.colorMode = CoatColorMode::InheritBase;
        c2.modulators.push_back({
            ModulatorSource::ArcLength, ModulatorWaveform::ValueNoise1D,
            ModulatorTarget::RadiusScale, 0.25f, 0.0f, 0.0f, 0.15f, 0.0f, 1.0f, 5u
        });
        BrushModulator hueMod;
        hueMod.source = ModulatorSource::ArcLength;
        hueMod.waveform = ModulatorWaveform::MirrorCycle;
        hueMod.target = ModulatorTarget::HueShiftDeg;
        hueMod.periodInBrushSizes = 10.0f; // Exact Google period = 10.0 x brush size
        hueMod.amplitude = 115.0f;        // Exact Google range = 2.007 rad (~115 deg)
        hueMod.bias = 0.0f;
        c2.modulators.push_back(hueMod);
        r.coats.push_back(c2);

        presets.push_back(r);
    }

    // =========================================================================
    // 2. Flag Banner (cahier: flag_banner.brushfamily)
    // Exact Google specification: inverted sharp triangle pennants (gap=1.2x baseWidth) + string
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Flag Banner";
        r.description = "Google Flag Banner: Hanging triangular pennants with rainbow hues along dark string.";

        // Coat 0: Pennant Flags (gravity-oriented downward sharp triangle, gap = 1.20 x baseWidth)
        BrushCoatSpec flags;
        flags.name = "Pennant Flags";
        flags.emissionMode = CoatEmissionMode::ParticleStamp;
        flags.particleOrientation = ParticleOrientation::GravityDown;
        flags.particleGapRatio = 1.20f;
        flags.particleLengthRatio = 1.20f;
        flags.widthScale = 1.0f;
        flags.radiusEndRatio = 0.04f; // Sharp pinch tip
        BrushModulator hueMod;
        hueMod.source = ModulatorSource::ParticleIndex;
        hueMod.waveform = ModulatorWaveform::Linear;
        hueMod.target = ModulatorTarget::HueShiftDeg;
        hueMod.frequency = 0.1666f; // 6-step rainbow cycle
        hueMod.amplitude = 360.0f;
        flags.modulators.push_back(hueMod);
        r.coats.push_back(flags);

        // Coat 1: Dark Hanging String (width = 0.10 x baseWidth)
        BrushCoatSpec stringCoat;
        stringCoat.name = "Banner String";
        stringCoat.emissionMode = CoatEmissionMode::ContinuousRibbon;
        stringCoat.widthScale = 0.10f;
        stringCoat.colorMode = CoatColorMode::FixedColor;
        stringCoat.fixedColor = ColorRgba(20, 20, 25, 255);
        r.coats.push_back(stringCoat);

        presets.push_back(r);
    }

    // =========================================================================
    // 3. Holiday Lights (cahier: holiday_lights.brushfamily)
    // Exact Google specification: 4 coats: double wire (+/-0.25 offset), sockets, alternating bulbs
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Holiday Lights";
        r.description = "Google Holiday Lights: twisted wires with sockets and bulbs that blink live while drawing.";
        r.isAnimated = true;
        r.settlingDurationSec = 0.65f;

        auto mk = [](ModulatorSource src, ModulatorWaveform wf, ModulatorTarget tg) {
            BrushModulator m; m.source = src; m.waveform = wf; m.target = tg; return m;
        };

        for (int w = 0; w < 2; ++w) {
            BrushCoatSpec wire;
            wire.name = (w == 0) ? "Twisted Wire A" : "Twisted Wire B";
            wire.widthScale = 0.12f;
            wire.colorMode = CoatColorMode::FixedColor;
            wire.fixedColor = (w == 0) ? ColorRgba(35, 45, 38, 255) : ColorRgba(25, 35, 28, 255);
            BrushModulator m = mk(ModulatorSource::ArcLength, ModulatorWaveform::SineWave, ModulatorTarget::LateralOffset);
            m.periodInBrushSizes = 5.0f;
            m.amplitudeRatio = 0.25f;
            m.phase = (w == 0) ? 0.0f : kPi;
            wire.modulators.push_back(m);
            r.coats.push_back(wire);
        }

        BrushCoatSpec sockets;
        sockets.name = "Bulb Sockets";
        sockets.emissionMode = CoatEmissionMode::ParticleStamp;
        sockets.particleOrientation = ParticleOrientation::PerpendicularAlternate;
        sockets.particleGapRatio = 2.0f;
        sockets.startSkipBrushSizes = 1.0f;
        sockets.lateralOffsetRatio = 0.20f;
        sockets.alternateLateralByParticle = true;
        sockets.particleLengthRatio = 0.40f;
        sockets.widthScale = 0.32f;
        sockets.radiusEndRatio = 0.90f;
        sockets.colorMode = CoatColorMode::FixedColor;
        sockets.fixedColor = ColorRgba(20, 28, 22, 255);
        r.coats.push_back(sockets);

        BrushCoatSpec bulbs;
        bulbs.name = "Glowing Bulbs";
        bulbs.emissionMode = CoatEmissionMode::ParticleStamp;
        bulbs.particleOrientation = ParticleOrientation::PerpendicularAlternate;
        bulbs.particleGapRatio = 2.0f;
        bulbs.startSkipBrushSizes = 1.0f;
        bulbs.lateralOffsetRatio = 0.45f;
        bulbs.alternateLateralByParticle = true;
        bulbs.particleLengthRatio = 0.80f;
        bulbs.widthScale = 0.40f;
        bulbs.radiusEndRatio = 0.65f;
        BrushModulator hue = mk(ModulatorSource::ParticleIndex, ModulatorWaveform::Linear, ModulatorTarget::HueShiftDeg);
        hue.frequency = 0.20f;
        hue.amplitude = 360.0f;
        bulbs.modulators.push_back(hue);
        BrushModulator blink = mk(ModulatorSource::TimeSinceInput, ModulatorWaveform::BlinkPulseClamped, ModulatorTarget::LightnessShift);
        blink.frequency = 1.666f;
        blink.amplitude = 0.42f;
        blink.bias = 0.10f;
        bulbs.modulators.push_back(blink);
        r.coats.push_back(bulbs);

        presets.push_back(r);
    }

    // =========================================================================
    // 4. Twisted Yarn (cahier: twisted_yarn.brushfamily)
    // Exact Google specification: 2 intertwined strands oscillating out of phase
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Twisted Yarn";
        r.description = "Google Twisted Yarn: two strands that pass over/under each other and swap colors at the crossings.";
        for (int k = 0; k < 2; ++k) {
            BrushCoatSpec c;
            c.name = (k == 0) ? "Yarn Strand A" : "Yarn Strand B";
            c.widthScale = 0.50f;
            BrushModulator lat;
            lat.source = ModulatorSource::ArcLength;
            lat.waveform = ModulatorWaveform::SineWave;
            lat.target = ModulatorTarget::LateralOffset;
            lat.periodInBrushSizes = 3.0f;
            lat.amplitudeRatio = 0.50f;
            lat.phase = (k == 0) ? 0.0f : kPi;
            c.modulators.push_back(lat);
            BrushModulator sw;
            sw.source = ModulatorSource::ArcLength;
            sw.waveform = ModulatorWaveform::AlternateSwap;
            sw.target = ModulatorTarget::HueShiftDeg;
            sw.periodInBrushSizes = 3.0f;
            sw.bias = 90.0f;
            sw.amplitude = (k == 0) ? 90.0f : -90.0f;
            sw.phase = kPi * 0.5f;
            c.modulators.push_back(sw);
            r.coats.push_back(c);
        }
        presets.push_back(r);
    }

    // =========================================================================
    // 5. Wet Paint (cahier: wet_paint.brushfamily)
    // Exact Google specification: Main paint ribbon + gravity drips (gap ~2.5x baseWidth)
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Wet Paint (Gravity Drips)";
        r.description = "Google Wet Paint: round drops form inside the stroke and slowly slide down over 1s, then stop.";
        r.isAnimated = true;
        r.settlingDurationSec = 1.05f;

        BrushCoatSpec drips;
        drips.name = "Gravity Drips";
        drips.emissionMode = CoatEmissionMode::ParticleStamp;
        drips.particleOrientation = ParticleOrientation::GravityDown;
        drips.particleGapRatio = 0.45f;
        drips.particleLengthRatio = 0.05f;
        drips.widthScale = 0.16f;
        drips.radiusEndRatio = 1.55f;
        BrushModulator len;
        len.source = ModulatorSource::TimeSinceInput;
        len.waveform = ModulatorWaveform::EaseOutDripSynced;
        len.target = ModulatorTarget::ParticleLength;
        len.frequency = 1.0f; len.bias = 1.0f; len.amplitude = 30.0f; len.seed = 0x0004071eu;
        drips.modulators.push_back(len);
        BrushModulator off;
        off.source = ModulatorSource::TimeSinceInput;
        off.waveform = ModulatorWaveform::EaseOutDripSynced;
        off.target = ModulatorTarget::WorldOffsetY;
        off.frequency = 1.0f; off.amplitudeRatio = 0.85f; off.seed = 0x0004071eu;
        drips.modulators.push_back(off);
        BrushModulator rad;
        rad.source = ModulatorSource::ArcLength;
        rad.waveform = ModulatorWaveform::ValueNoise1D;
        rad.target = ModulatorTarget::RadiusScale;
        rad.periodInBrushSizes = 1.40f; rad.bias = 1.10f; rad.amplitude = 0.36f; rad.seed = 0x000b79dfu;
        drips.modulators.push_back(rad);
        r.coats.push_back(drips);

        BrushCoatSpec paint;
        paint.name = "Paint Base Ribbon";
        paint.widthScale = 0.92f;
        r.coats.push_back(paint);

        presets.push_back(r);
    }

    // =========================================================================
    // 6. 3D Drop Shadow (cahier: shadow.brushfamily)
    // Exact Google specification: Darkened offset shadow coat (0.65 x baseWidth, alpha 0.6) + foreground
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "3D Drop Shadow";
        r.description = "Google Shadow: offset darkened copy of the base color beneath the stroke.";

        BrushCoatSpec shadow;
        shadow.name = "Cast Shadow";
        shadow.widthScale = 1.05f;
        shadow.worldOffsetXRatio = 0.45f;
        shadow.worldOffsetYRatio = 0.65f;
        shadow.opacityScale = 0.65f;
        shadow.colorMode = CoatColorMode::InheritBase;
        shadow.lightnessOffset = -0.22f;
        r.coats.push_back(shadow);

        BrushCoatSpec fg;
        fg.name = "Foreground Stroke";
        fg.widthScale = 1.0f;
        r.coats.push_back(fg);

        presets.push_back(r);
    }

    // =========================================================================
    // 7. Calligraphy Chisel (cahier: calligraphy.brushfamily)
    // Exact Google specification: Scale 0.25 to 1.50 based on downward travel direction
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Calligraphy Chisel";
        r.description = "Google Calligraphy: Direction-sensitive chisel nib with thick downstrokes and thin cross-strokes.";

        BrushCoatSpec chisel;
        chisel.name = "Chisel Nib";
        chisel.widthScale = 0.50f;
        BrushModulator dirMod;
        dirMod.source = ModulatorSource::TravelAngle;
        dirMod.waveform = ModulatorWaveform::SineWave;
        dirMod.target = ModulatorTarget::RadiusScale;
        dirMod.amplitude = 0.85f;
        dirMod.bias = 1.15f;
        dirMod.phase = -0.785398f; // -45 deg slant
        chisel.modulators.push_back(dirMod);
        r.coats.push_back(chisel);

        presets.push_back(r);
    }

    // =========================================================================
    // 8. Pressure Wave (cahier: pressure_wave.brushfamily)
    // Exact Google specification: Main stroke + oscillating wave (amplitude=0.38 x baseWidth)
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Pressure Wave";
        r.description = "Google Pressure Wave: Base stroke with an undulating internal wave scaling with pressure.";

        // Coat 0: Base Envelope
        BrushCoatSpec base;
        base.name = "Base Stroke";
        base.widthScale = 0.80f;
        BrushModulator pMod;
        pMod.source = ModulatorSource::Pressure;
        pMod.waveform = ModulatorWaveform::Linear;
        pMod.target = ModulatorTarget::RadiusScale;
        pMod.bias = 0.75f;
        pMod.amplitude = 0.50f;
        base.modulators.push_back(pMod);
        r.coats.push_back(base);

        // Coat 1: Wave
        BrushCoatSpec wave;
        wave.name = "Oscillating Wave";
        wave.widthScale = 0.25f;
        BrushModulator waveMod;
        waveMod.source = ModulatorSource::ArcLength;
        waveMod.waveform = ModulatorWaveform::SineWave;
        waveMod.target = ModulatorTarget::LateralOffset;
        waveMod.periodInBrushSizes = 2.0f;
        waveMod.amplitudeRatio = 0.38f;
        wave.modulators.push_back(waveMod);
        r.coats.push_back(wave);

        presets.push_back(r);
    }

    // =========================================================================
    // 9. Music Staff (cahier: music.brushfamily)
    // Exact Google specification: 5 staff lines (offsets 0, +/-0.35, +/-0.70) + stamped notes
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Music Staff";
        r.description = "Google Music: 5 continuous staff lines with scattered musical notes and bar marks.";

        // 5 Staff Lines
        const float staffOffsets[5] = { -0.70f, -0.35f, 0.0f, 0.35f, 0.70f };
        for (int i = 0; i < 5; ++i) {
            BrushCoatSpec line;
            line.name = std::string("Staff Line ") + std::to_string(i + 1);
            line.widthScale = 0.05f;
            line.lateralOffsetRatio = staffOffsets[i];
            line.colorMode = CoatColorMode::FixedColor;
            line.fixedColor = ColorRgba(30, 30, 35, 220);
            r.coats.push_back(line);
        }

        // Coat 5: Quarter Notes
        BrushCoatSpec notes;
        notes.name = "Quarter Notes";
        notes.emissionMode = CoatEmissionMode::ParticleStamp;
        notes.particleGapRatio = 1.20f;
        notes.particleLengthRatio = 0.70f;
        notes.widthScale = 0.35f;
        notes.radiusEndRatio = 0.70f;
        notes.particleOrientation = ParticleOrientation::FollowTangent;
        notes.colorMode = CoatColorMode::FixedColor;
        notes.fixedColor = ColorRgba(20, 20, 25, 255);
        BrushModulator noteMod;
        noteMod.source = ModulatorSource::ParticleIndex;
        noteMod.waveform = ModulatorWaveform::HashRandom;
        noteMod.target = ModulatorTarget::LateralOffset;
        noteMod.amplitudeRatio = 0.50f;
        noteMod.bias = -0.25f;
        noteMod.seed = 777u;
        notes.modulators.push_back(noteMod);
        r.coats.push_back(notes);

        // Coat 6: Bar Lines
        BrushCoatSpec bar;
        bar.name = "Measure Bar Lines";
        bar.emissionMode = CoatEmissionMode::ParticleStamp;
        bar.particleGapRatio = 4.00f;
        bar.particleLengthRatio = 1.40f;
        bar.widthScale = 0.06f;
        bar.radiusEndRatio = 1.0f;
        bar.particleOrientation = ParticleOrientation::PerpendicularAlternate;
        bar.colorMode = CoatColorMode::FixedColor;
        bar.fixedColor = ColorRgba(30, 30, 35, 200);
        r.coats.push_back(bar);

        presets.push_back(r);
    }

    // =========================================================================
    // 10. Jelly Wobble (cahier: jelly_wobble.brushfamily)
    // Exact Google specification: Dynamic ripple along path and time
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Jelly Wobble";
        r.description = "Google Jelly Wobble: live traveling ripple that slows to rest after pen lift.";
        r.isAnimated = true;
        r.settlingDurationSec = 0.95f;

        BrushModulator rad;
        rad.source = ModulatorSource::TimeSinceInput;
        rad.waveform = ModulatorWaveform::TravelingWaveJelly;
        rad.target = ModulatorTarget::RadiusScale;
        rad.bias = 1.0f; rad.amplitude = 0.25f;
        BrushModulator lat = rad;
        lat.target = ModulatorTarget::LateralOffset;
        lat.bias = 0.0f; lat.amplitude = 0.0f; lat.amplitudeRatio = 0.15f;

        BrushCoatSpec body;
        body.name = "Gelatin Base";
        body.widthScale = 1.10f;
        body.opacityScale = 0.85f;
        body.modulators.push_back(rad);
        body.modulators.push_back(lat);
        r.coats.push_back(body);

        BrushCoatSpec glint;
        glint.name = "Glint Highlight";
        glint.widthScale = 0.30f;
        glint.lateralOffsetRatio = -0.30f;
        glint.colorMode = CoatColorMode::FixedColor;
        glint.fixedColor = ColorRgba(255, 255, 255, 220);
        glint.modulators.push_back(lat);
        r.coats.push_back(glint);

        presets.push_back(r);
    }

    // =========================================================================
    // 11. Groovy Disco (cahier: groovy.brushfamily)
    // Exact Google specification: 4 stacked retro coats (offsets X=0.6, Y=1.0, hues 4.538 rad / -0.611 rad)
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Groovy (1970s Disco)";
        r.description = "Google Groovy: 4 stacked retro ribbons with stepped isometric offsets and 1970s hue shifts.";

        const struct { const char* name; float ox; float oy; float h; float l; float w; } kGroovy[4] = {
            { "Deep Outline", 0.60f, 1.00f, -35.0f, -0.65f, 1.10f },
            { "Mid Tone",     0.30f, 0.50f, 180.0f, -0.40f, 1.00f },
            { "Underline",    0.15f, 0.25f,  90.0f, -0.15f, 0.90f },
            { "Front Stripe", 0.00f, 0.00f,   0.0f,   0.0f, 0.60f }
        };

        for (int i = 0; i < 4; ++i) {
            BrushCoatSpec c;
            c.name = kGroovy[i].name;
            c.widthScale = kGroovy[i].w;
            c.worldOffsetXRatio = kGroovy[i].ox;
            c.worldOffsetYRatio = kGroovy[i].oy;
            c.hueOffsetDeg = kGroovy[i].h;
            c.lightnessOffset = kGroovy[i].l;
            r.coats.push_back(c);
        }
        presets.push_back(r);
    }

    // =========================================================================
    // 12. Lace Ribbon (cahier: lace.brushfamily)
    // Exact Google specification: 4 filigree strands oscillating in quad phase (0, pi/2, pi, 3pi/2)
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Lace Ribbon";
        r.description = "Google Lace: 4 interlaced delicate filigree strands oscillating in quad phase.";

        const struct { float phase; float l; } kLace[4] = {
            { 0.0f,         0.0f  },
            { kPi * 0.5f,   0.15f },
            { kPi,         -0.10f },
            { kPi * 1.5f,   0.20f }
        };

        for (int i = 0; i < 4; ++i) {
            BrushCoatSpec c;
            c.name = std::string("Lace Strand ") + std::to_string(i + 1);
            c.widthScale = 0.20f;
            c.opacityScale = 0.85f;
            c.lightnessOffset = kLace[i].l;

            BrushModulator laceMod;
            laceMod.source = ModulatorSource::ArcLength;
            laceMod.waveform = ModulatorWaveform::SineWave;
            laceMod.target = ModulatorTarget::LateralOffset;
            laceMod.periodInBrushSizes = 3.0f;
            laceMod.amplitudeRatio = 0.35f;
            laceMod.phase = kLace[i].phase;
            c.modulators.push_back(laceMod);

            r.coats.push_back(c);
        }
        presets.push_back(r);
    }

    return presets;
}

} // namespace Folio
