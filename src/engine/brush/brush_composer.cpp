/**
 * =========================================================================================
 * @file src/engine/brush_composer.cpp
 * @brief Universal Multi-Coat & Signal-Modulator Inking Brush Composer Implementation
 * =========================================================================================
 */

#include "engine/brush/brush_composer.hpp"
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
            return rawLinearArg;

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
            // Google jelly_wobble: smooth width undulation along stroke over time + EaseOut settling over 0.90s after pen lift
            float bw = std::max(ctx.baseWidth, 1.0f);
            float distCycles = ctx.arcLength / (bw * 3.8f);
            float activePhaseTime = ctx.timeSinceInput;
            if (ctx.timeSinceStrokeEnd > 0.0f) {
                float uEnd = std::clamp(ctx.timeSinceStrokeEnd / 0.90f, 0.0f, 1.0f);
                float inv = 1.0f - uEnd;
                float easeEnd = 1.0f - inv * inv * inv;
                float ageAtPenUp = std::max(0.0f, ctx.timeSinceInput - ctx.timeSinceStrokeEnd);
                activePhaseTime = ageAtPenUp + easeEnd * 0.90f;
            }
            return std::sin((distCycles - activePhaseTime * 2.0f) * kTwoPi);
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
        // When amplitudeRatio is used the bias is also expressed in brush sizes
        float effBias = (mod.amplitudeRatio != 0.0f) ? (mod.bias * baseWidth) : mod.bias;
        float outVal = effBias + effAmp * wave;

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

    auto emit = [&](CapsuleVec2 a, CapsuleVec2 b, float ra, float rb, uint32_t c) {
        CapsuleInstance i{};
        i.pointA = a; i.pointB = b; i.radii = { ra, rb };
        i.colorRgba = c; i.colorRgbaEnd = c;
        outInstances.push_back(i);
    };

    // 2. Synthesize each coat sequentially
    for (const auto& coat : recipe.coats) {
        if (coat.emissionMode == CoatEmissionMode::YarnStrandBumps) {
            const float period = std::max(coat.yarnPeriodBrushSizes, 0.5f) * baseWidth;
            const float amp    = coat.yarnAmplitudeRatio * baseWidth;
            const float R0     = baseWidth * 0.5f * coat.widthScale;
            const float step   = std::max((coat.particleGapRatio > 0.0f) ? coat.particleGapRatio * baseWidth : 1.45f * R0, 0.4f);
            size_t seg = 0;
            uint32_t idx = 0;
            for (float s = 0.0f; s <= totalLen + step * 0.5f; s += step, ++idx) {
                const float sc = std::min(s, totalLen);
                while (seg + 2 < N && arcLengths[seg + 1] < sc) ++seg;
                const float sl = arcLengths[seg + 1] - arcLengths[seg];
                const float t = (sl > 1e-5f) ? std::clamp((sc - arcLengths[seg]) / sl, 0.0f, 1.0f) : 0.0f;
                const float px = static_cast<float>(points[seg].x + (points[seg + 1].x - points[seg].x) * t);
                const float py = static_cast<float>(points[seg].y + (points[seg + 1].y - points[seg].y) * t);
                const float pr = points[seg].r + (points[seg + 1].r - points[seg].r) * t;
                const CapsuleVec2 nrm{ normals[seg].x, normals[seg].y };
                const float th = kTwoPi * sc / period;
                const float latA = amp * std::sin(th);
                // The strand moving toward -normal passes OVER; the swap happens at the sway extremes (no overlap there)
                const bool aFront = std::cos(th) < 0.0f;
                for (int o = 0; o < 2; ++o) {
                    const int strand = (o == 0) ? (aFront ? 1 : 0) : (aFront ? 0 : 1);
                    const float lat = (strand == 0) ? latA : -latA;
                    const float jitter = 0.88f + 0.24f * Hash1D(idx * 2u + static_cast<uint32_t>(strand), 77u);
                    const float R = ((pr > 0.01f) ? pr : baseWidth * 0.5f) * coat.widthScale * jitter;
                    const float light = ((idx & 1u) ? 0.05f : -0.03f) + ((o == 0) ? -0.10f : 0.0f);
                    float al = 1.0f;
                    ColorRgba col = ComputeCoatColor(coat, baseColor, (strand == 1) ? coat.hueOffsetDeg : 0.0f, light, al);
                    CapsuleVec2 pos{ px + nrm.x * lat, py + nrm.y * lat };
                    emit(pos, pos, R, R, PackPremultipliedGpuColor(col, al * coat.opacityScale));
                }
            }
            continue;
        }
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

                float alphaA = 1.0f, alphaB = 1.0f;
                ColorRgba colA = ComputeCoatColor(coat, baseColor, modA.hueShiftDeg, modA.lightnessShift, alphaA);
                ColorRgba colB = ComputeCoatColor(coat, baseColor, modB.hueShiftDeg, modB.lightnessShift, alphaB);

                CapsuleInstance inst{};
                inst.pointA = posA;
                inst.pointB = posB;
                inst.radii = { rA, rB };
                inst.colorRgba    = PackPremultipliedGpuColor(colA, alphaA * modA.opacityScale);
                inst.colorRgbaEnd = PackPremultipliedGpuColor(colB, alphaB * modB.opacityScale);

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
                    case ParticleOrientation::NormalHangDown:
                        dir = norm;
                        if (dir.y < 0.0f) dir = { -dir.x, -dir.y };
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
                const uint32_t packed = PackPremultipliedGpuColor(col, finalAlpha);

                if (coat.emissionMode == CoatEmissionMode::MusicalNotes) {
                    const float bw = baseWidth;
                    auto P = [&](float u, float v) {
                        return CapsuleVec2{ px + tan.x * u * bw - norm.x * v * bw, py + tan.y * u * bw - norm.y * v * bw };
                    };
                    if (pIdx == 0) {
                        // Treble clef: vertical spine + spiral, in brush-size units (v is "up")
                        static const float kClef[][2] = {
                            {0.02f, 0.95f}, {-0.12f, 0.78f}, {-0.12f, 0.55f}, {0.04f, 0.32f}, {0.16f, 0.05f},
                            {0.12f, -0.25f}, {0.0f, -0.40f}, {-0.14f, -0.32f}, {-0.10f, -0.10f}, {0.08f, -0.08f},
                            {0.14f, 0.12f}, {0.05f, 0.30f}, {-0.10f, 0.28f}
                        };
                        for (size_t k = 0; k + 1 < sizeof(kClef) / sizeof(kClef[0]); ++k) {
                            emit(P(kClef[k][0], kClef[k][1]), P(kClef[k + 1][0], kClef[k + 1][1]), 0.035f * bw, 0.035f * bw, packed);
                        }
                        emit(P(0.02f, 0.95f), P(0.04f, -0.78f), 0.03f * bw, 0.03f * bw, packed);
                        emit(P(0.04f, -0.78f), P(-0.06f, -0.90f), 0.04f * bw, 0.04f * bw, packed);
                    } else if (pIdx >= 2) {
                        const int step = static_cast<int>(Hash1D(pIdx, coat.modulators.empty() ? 4242u : coat.modulators[0].seed) * 6.999f);
                        const float v = (static_cast<float>(step) - 3.0f) * 0.175f;
                        const bool up = (v < 0.18f);
                        const float sgn = up ? 1.0f : -1.0f;
                        emit(P(-0.05f, v - 0.03f), P(0.05f, v + 0.03f), 0.115f * bw, 0.115f * bw, packed);
                        const float sx = up ? 0.105f : -0.105f;
                        const float top = v + 0.85f * sgn;
                        emit(P(sx, v), P(sx, top), 0.024f * bw, 0.024f * bw, packed);
                        if (pIdx % 3 == 0) {
                            emit(P(sx, top), P(sx + 0.16f, top - 0.20f * sgn), 0.034f * bw, 0.022f * bw, packed);
                            emit(P(sx + 0.16f, top - 0.20f * sgn), P(sx + 0.12f, top - 0.46f * sgn), 0.022f * bw, 0.016f * bw, packed);
                        }
                    }
                    continue;
                }

                if (coat.triangleFan) {
                    // Flat-based triangle: base along the stroke, apex hanging toward 'dir'
                    const float W = ((pr > 0.01f) ? pr : baseWidth * 0.5f) * coat.widthScale;
                    const float H = std::max(lenBase * mod.particleLength, 0.1f);
                    constexpr int K = 9;
                    const float rr = W / K * 1.15f;
                    const CapsuleVec2 apex{ center.x + dir.x * H, center.y + dir.y * H };
                    for (int k = 0; k < K; ++k) {
                        const float o = -(W - rr) + 2.0f * (W - rr) * (static_cast<float>(k) + 0.5f) / K;
                        CapsuleVec2 b0{ center.x + tan.x * o + dir.x * rr, center.y + tan.y * o + dir.y * rr };
                        emit(b0, apex, rr, rr * 0.12f, packed);
                    }
                    continue;
                }

                CapsuleInstance inst{};
                inst.pointA = startPos;
                inst.pointB = endPos;
                inst.radii = { rStart, rEnd };
                inst.colorRgba = packed;
                inst.colorRgbaEnd = packed;
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
    // Graffiti Spray (cahier: graffiti.brushfamily) - opaque halo + black outline + hue-mirrored core
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Graffiti Spray";
        r.description = "Google Graffiti: base-color halo, thick black outline and a core whose hue shifts smoothly along the stroke.";
        auto noise = [](uint32_t seed, float lo, float hi) {
            BrushModulator m;
            m.source = ModulatorSource::ArcLength; m.waveform = ModulatorWaveform::ValueNoise1D;
            m.target = ModulatorTarget::RadiusScale; m.frequency = 0.2f;
            m.bias = (lo + hi) * 0.5f; m.amplitude = (hi - lo) * 0.5f; m.seed = seed; return m;
        };
        BrushCoatSpec c0; c0.name = "Outer Halo"; c0.widthScale = 1.30f; c0.modulators.push_back(noise(3u, 0.95f, 1.05f)); r.coats.push_back(c0);
        BrushCoatSpec c1; c1.name = "Black Outline"; c1.widthScale = 1.05f; c1.colorMode = CoatColorMode::FixedColor;
        c1.fixedColor = ColorRgba(12, 12, 16, 255); c1.modulators.push_back(noise(3u, 0.95f, 1.10f)); r.coats.push_back(c1);
        BrushCoatSpec c2; c2.name = "Hue-Shifting Core"; c2.widthScale = 0.55f; c2.modulators.push_back(noise(3u, 0.8f, 1.2f));
        BrushModulator hue; hue.source = ModulatorSource::ArcLength; hue.waveform = ModulatorWaveform::MirrorCycle;
        hue.target = ModulatorTarget::HueShiftDeg; hue.periodInBrushSizes = 10.0f; hue.amplitude = 115.0f;
        c2.modulators.push_back(hue); r.coats.push_back(c2);
        presets.push_back(r);
    }

    // =========================================================================
    // Flag Banner (cahier: flag_banner.brushfamily) - flat-based triangles hanging from a string
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Flag Banner";
        r.description = "Google Flag Banner: rainbow triangular flags whose flat top sits on the string and hang downward.";
        BrushCoatSpec flags;
        flags.name = "Pennant Flags";
        flags.emissionMode = CoatEmissionMode::ParticleStamp;
        flags.triangleFan = true;
        flags.particleOrientation = ParticleOrientation::NormalHangDown;
        flags.particleGapRatio = 1.20f;
        flags.particleLengthRatio = 1.25f;
        flags.startSkipBrushSizes = 0.7f;
        flags.endSkipBrushSizes = 0.7f;
        flags.widthScale = 0.95f;
        BrushModulator hue; hue.source = ModulatorSource::ParticleIndex; hue.waveform = ModulatorWaveform::Linear;
        hue.target = ModulatorTarget::HueShiftDeg; hue.frequency = 1.0f; hue.amplitude = 60.0f;
        flags.modulators.push_back(hue);
        r.coats.push_back(flags);
        BrushCoatSpec str; str.name = "String"; str.widthScale = 0.10f; str.colorMode = CoatColorMode::FixedColor;
        str.fixedColor = ColorRgba(15, 15, 18, 255); r.coats.push_back(str);
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
    // Twisted Yarn (cahier: twisted_yarn.brushfamily) - two bumpy strands weaving over/under
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Twisted Yarn";
        r.description = "Google Twisted Yarn: two solid-color strands built from stacked round bumps that weave over and under each other.";
        BrushCoatSpec y;
        y.name = "Yarn Strands";
        y.emissionMode = CoatEmissionMode::YarnStrandBumps;
        y.widthScale = 0.55f;
        y.yarnPeriodBrushSizes = 3.0f;
        y.yarnAmplitudeRatio = 0.38f;
        y.hueOffsetDeg = 180.0f; // second strand = opposite hue
        r.coats.push_back(y);
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
    // Pens: Ballpoint, Marker, Fountain, Highlighter
    // =========================================================================
    {
        BrushRecipe r; r.name = "Ballpoint Pen"; r.description = "Thin, crisp, near-constant-width line.";
        r.usePressure = true;
        BrushCoatSpec c; c.name = "Ink"; c.widthScale = 0.40f; r.coats.push_back(c);
        presets.push_back(r);
    }
    {
        BrushRecipe r; r.name = "Marker"; r.description = "Wide opaque felt-tip marker with constant width.";
        r.usePressure = false;
        BrushCoatSpec c; c.name = "Felt"; c.widthScale = 1.10f; r.coats.push_back(c);
        presets.push_back(r);
    }
    {
        BrushRecipe r; r.name = "Fountain Pen"; r.description = "Pressure and speed sensitive line: thick when slow, thin when fast.";
        r.usePressure = true; r.useVelocity = true;
        BrushCoatSpec c; c.name = "Nib"; c.widthScale = 0.80f; r.coats.push_back(c);
        presets.push_back(r);
    }
    {
        BrushRecipe r; r.name = "Highlighter"; r.description = "Wide translucent stroke; overlaps never darken (set-union), joins are seamless.";
        r.usePressure = false; r.opacity = 0.38f; r.unionOverlap = true;
        BrushCoatSpec c; c.name = "Highlight"; c.widthScale = 1.9f; r.coats.push_back(c);
        presets.push_back(r);
    }
    // =========================================================================
    // Shading Pencil (cahier: shading_pencil.brushfamily)
    // Grainy graphite body: opacity follows pressure, width grows with tilt, plus loose graphite specks
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Shading Pencil";
        r.description = "Google Shading Pencil: paper-grain graphite; light pressure is faint, tilt widens it to shade, specks add texture.";
        r.usePressure = false; r.useTilt = true;
        r.grain = 0.85f; r.grainScale = 2.4f; r.unionOverlap = true;
        BrushCoatSpec body; body.name = "Graphite Body"; body.widthScale = 0.55f;
        BrushModulator op; op.source = ModulatorSource::Pressure; op.waveform = ModulatorWaveform::Linear;
        op.target = ModulatorTarget::OpacityScale; op.frequency = 1.0f; op.bias = 0.12f; op.amplitude = 0.88f;
        body.modulators.push_back(op);
        BrushModulator tilt; tilt.source = ModulatorSource::Tilt; tilt.waveform = ModulatorWaveform::Linear;
        tilt.target = ModulatorTarget::RadiusScale; tilt.frequency = 0.83f; tilt.bias = 1.0f; tilt.amplitude = 2.0f;
        body.modulators.push_back(tilt);
        r.coats.push_back(body);
        BrushCoatSpec specks; specks.name = "Graphite Specks";
        specks.emissionMode = CoatEmissionMode::ParticleStamp;
        specks.particleGapRatio = 0.22f; specks.particleLengthRatio = 0.0f; specks.particleLengthMm = 0.1f;
        specks.widthScale = 0.10f; specks.opacityScale = 0.55f;
        BrushModulator lat; lat.source = ModulatorSource::ParticleIndex; lat.waveform = ModulatorWaveform::HashRandom;
        lat.target = ModulatorTarget::LateralOffset; lat.amplitudeRatio = 0.9f; lat.bias = -0.5f; lat.seed = 31u;
        specks.modulators.push_back(lat);
        BrushModulator sz; sz.source = ModulatorSource::ParticleIndex; sz.waveform = ModulatorWaveform::HashRandom;
        sz.target = ModulatorTarget::RadiusScale; sz.amplitude = 1.2f; sz.bias = 0.4f; sz.seed = 57u;
        specks.modulators.push_back(sz);
        r.coats.push_back(specks);
        presets.push_back(r);
    }

    // =========================================================================
    // Music Staff (cahier: music.brushfamily) - staff lines, treble clef, notes with stems/flags, bar lines
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Music Staff";
        r.description = "Google Music: five staff lines carrying a treble clef, real notes (head, stem, flags) and bar lines.";
        const float staffOffsets[5] = { -0.70f, -0.35f, 0.0f, 0.35f, 0.70f };
        for (int i = 0; i < 5; ++i) {
            BrushCoatSpec line; line.name = std::string("Staff Line ") + std::to_string(i + 1);
            line.widthScale = 0.05f; line.lateralOffsetRatio = staffOffsets[i]; r.coats.push_back(line);
        }
        BrushCoatSpec notes; notes.name = "Clef & Notes";
        notes.emissionMode = CoatEmissionMode::MusicalNotes;
        notes.particleGapRatio = 1.20f; notes.particleOrientation = ParticleOrientation::FollowTangent;
        BrushModulator seedMod; seedMod.seed = 4242u; seedMod.amplitude = 0.0f; seedMod.target = ModulatorTarget::OpacityScale; seedMod.bias = 1.0f;
        notes.modulators.push_back(seedMod);
        r.coats.push_back(notes);
        BrushCoatSpec bar; bar.name = "Bar Lines";
        bar.emissionMode = CoatEmissionMode::ParticleStamp; bar.particleGapRatio = 4.0f;
        bar.particleLengthRatio = 1.40f; bar.widthScale = 0.05f;
        bar.particleOrientation = ParticleOrientation::PerpendicularAlternate;
        bar.lateralOffsetRatio = 0.0f; bar.startSkipBrushSizes = 2.5f;
        r.coats.push_back(bar);
        presets.push_back(r);
    }

    // =========================================================================
    // Jelly Wobble (cahier: jelly_wobble.brushfamily) - single color, smoothly undulating width
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Jelly Wobble";
        r.description = "Google Jelly Wobble: one unified color; width undulates smoothly (0.75x-1.25x) while drawing and settles after pen lift.";
        r.isAnimated = true;
        r.settlingDurationSec = 0.95f;
        BrushModulator rad;
        rad.source = ModulatorSource::TimeSinceInput;
        rad.waveform = ModulatorWaveform::TravelingWaveJelly;
        rad.target = ModulatorTarget::RadiusScale;
        rad.bias = 1.0f; rad.amplitude = 0.25f;
        BrushCoatSpec body; body.name = "Jelly"; body.widthScale = 1.0f;
        body.modulators.push_back(rad);
        r.coats.push_back(body);
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

BrushRecipe BrushComposer::GetPresetRecipe(BrushPreset preset) {
    auto allPresets = CreateCahierPresets();

    auto findByName = [&](std::string_view name) -> BrushRecipe {
        for (const auto& p : allPresets) {
            if (p.name == name) return p;
        }
        return allPresets.empty() ? BrushRecipe{} : allPresets.front();
    };

    switch (preset) {
        case BrushPreset::BallpointPen:  return findByName("Ballpoint Pen");
        case BrushPreset::Marker:        return findByName("Marker");
        case BrushPreset::FountainPen:   return findByName("Fountain Pen");
        case BrushPreset::Highlighter:   return findByName("Highlighter");
        case BrushPreset::Pencil:        return findByName("Shading Pencil");
        case BrushPreset::WetPaint:      return findByName("Wet Paint (Dynamic Drips)");
        case BrushPreset::Graffiti:      return findByName("Graffiti Spray");
        case BrushPreset::HolidayLights: return findByName("Holiday Lights");
        case BrushPreset::TwistedYarn:   return findByName("Twisted Yarn");
        case BrushPreset::MusicStaff:    return findByName("Music Staff");
        case BrushPreset::Custom: {
            BrushRecipe r;
            r.name = "Custom";
            BrushCoatSpec c;
            c.name = "Base Coat";
            r.coats.push_back(c);
            return r;
        }
    }
    return findByName("Ballpoint Pen");
}

BrushRecipe BrushComposer::RecipeFromStyle(const StrokeStyle& style) {
    BrushRecipe recipe = GetPresetRecipe(BrushPreset::BallpointPen);

    // Apply any explicit overrides from the StrokeStyle
    recipe.usePressure = style.usePressure;
    recipe.useVelocity = style.useVelocity;
    recipe.useTilt     = style.useTilt;

    return recipe;
}

BrushRecipe BrushComposer::CreateBallpoint() { return GetPresetRecipe(BrushPreset::BallpointPen); }
BrushRecipe BrushComposer::CreateMarker() { return GetPresetRecipe(BrushPreset::Marker); }
BrushRecipe BrushComposer::CreateFountain() { return GetPresetRecipe(BrushPreset::FountainPen); }
BrushRecipe BrushComposer::CreateHighlighter() { return GetPresetRecipe(BrushPreset::Highlighter); }
BrushRecipe BrushComposer::CreatePencil() { return GetPresetRecipe(BrushPreset::Pencil); }

} // namespace Folio
