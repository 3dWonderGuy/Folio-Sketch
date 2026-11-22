/**
 * =========================================================================================
 * @file src/pipelines/capsule/ink_capsule_builder.cpp
 * @brief High-Performance GPU Capsule SDF Instance Streaming Pipeline
 * =========================================================================================
 */

#include "pipelines/capsule/ink_capsule.hpp"
#include "engine/brush/brush_modulator.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Folio {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

/**
 * Handles 3 distinct operational cases:
 * 1. Single tap dot (N == 1):
 *    Emits a degenerate circle capsule where pointA == pointB and radii = (r0, r0).
 * 2. Full stroke build (fromSegment == 0):
 *    Clears the destination and builds all N - 1 segments from scratch.
 * 3. Incremental live build (fromSegment > 0):
 *    Retains previously built instances and only appends new segments [fromSegment, N - 1)
 *    in O(1) time per digitizer sample.
 */
void CapsuleBuilder::BuildCapsuleInstances(std::span<const StrokePoint> points,
                                           uint32_t colorRgba,
                                           std::vector<CapsuleInstance>& outInstances,
                                           size_t fromSegment) {
    const size_t n = points.size();
    if (n == 0) {
        outInstances.clear();
        return;
    }

    // Case 1: Single-point stationary tap dot
    if (n == 1) {
        outInstances.clear();
        const auto& p0 = points[0];
        const float x0 = static_cast<float>(p0.x);
        const float y0 = static_cast<float>(p0.y);
        const float r0 = p0.r > 0.0f ? p0.r : 0.0f;
        outInstances.push_back(CapsuleInstance{
            CapsuleVec2(x0, y0),
            CapsuleVec2(x0, y0),
            CapsuleVec2(r0, r0),
            colorRgba,
            colorRgba
        });
        return;
    }

    // N >= 2 points make N - 1 segments
    const size_t segCount = n - 1;
    if (fromSegment >= segCount) return;

    // Case 2: Full rebuild (fromSegment == 0) clears outInstances to rebuild from scratch.
    if (fromSegment == 0) {
        outInstances.clear();
    }
    
    // Case 3: Incremental build (fromSegment > 0) preserves slots [0, fromSegment - 1]
    // and only expands the buffer to accommodate the newly added segments.
    outInstances.resize(segCount);

    const StrokePoint* src = points.data();
    CapsuleInstance* dst = outInstances.data();

    // Loops strictly over the newly appended segments [fromSegment, segCount) in O(1) time
    for (size_t i = fromSegment; i < segCount; ++i) {
        const StrokePoint& pA = src[i];
        const StrokePoint& pB = src[i + 1];
        dst[i].pointA.x     = static_cast<float>(pA.x);
        dst[i].pointA.y     = static_cast<float>(pA.y);
        dst[i].pointB.x     = static_cast<float>(pB.x);
        dst[i].pointB.y     = static_cast<float>(pB.y);
        dst[i].radii.x      = pA.r > 0.0f ? pA.r : 0.0f;
        dst[i].radii.y      = pB.r > 0.0f ? pB.r : 0.0f;
        dst[i].colorRgba    = colorRgba;
        dst[i].colorRgbaEnd = colorRgba;
    }
}

void CapsuleBuilder::BuildRecipeInstances(
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
    std::vector<float> speeds(N, 0.0f);
    std::vector<float> turnAngles(N, 0.0f);
    std::vector<float> flows(N, 1.0f);
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

        double dt = points[i].timestampSec - points[i - 1].timestampSec;
        float segSpeed = (dt > 1e-5) ? static_cast<float>(segLen / dt) : points[i].speed;
        speeds[i] = (segSpeed > 0.0f) ? segSpeed : points[i].speed;

        if (segLen > 1e-5f) {
            tangents[i - 1] = { dx / segLen, dy / segLen };
            normals[i - 1]  = { -dy / segLen, dx / segLen };
        } else {
            tangents[i - 1] = { 1.0f, 0.0f };
            normals[i - 1]  = { 0.0f, 1.0f };
        }

        if (i >= 2) {
            float dot = tangents[i - 1].x * tangents[i - 2].x + tangents[i - 1].y * tangents[i - 2].y;
            dot = std::clamp(dot, -1.0f, 1.0f);
            turnAngles[i - 1] = std::acos(dot);
        }

        // Flow factor calculation: higher speed -> hydrodynamic thinning
        float maxSpd = 1500.0f;
        float normSpeed = std::clamp(speeds[i] / maxSpd, 0.0f, 1.0f);
        flows[i] = 1.0f - normSpeed * 0.7f;
    }
    tangents[N - 1] = tangents[N - 2];
    normals[N - 1]  = normals[N - 2];
    speeds[0]       = speeds[1];
    flows[0]        = flows[1];
    turnAngles[N - 1] = turnAngles[N - 2];

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
                    const float jitter = 0.88f + 0.24f * ModulatorEvaluator::Hash1D(idx * 2u + static_cast<uint32_t>(strand), 77u);
                    const float R = ((pr > 0.01f) ? pr : baseWidth * 0.5f) * coat.widthScale * jitter;
                    const float light = ((idx & 1u) ? 0.05f : -0.03f) + ((o == 0) ? -0.10f : 0.0f);
                    float al = 1.0f;
                    ColorRgba col = ModulatorEvaluator::ComputeCoatColor(coat, baseColor, (strand == 1) ? coat.hueOffsetDeg : 0.0f, light, al);
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
                    arcLengths[i], totalLen - arcLengths[i], ptA.pressure, pIntegrals[i], speeds[i],
                    std::sqrt(ptA.tiltX * ptA.tiltX + ptA.tiltY * ptA.tiltY),
                    std::atan2(tangents[i].y, tangents[i].x),
                    arcLengths[i] * invTotalLen,
                    0.0f, timeSeconds, ageOf(ptA.timestampSec), tEnd, baseWidth,
                    flows[i], turnAngles[i]
                };
                PointContext ctxB{
                    arcLengths[i + 1], totalLen - arcLengths[i + 1], ptB.pressure, pIntegrals[i + 1], speeds[i + 1],
                    std::sqrt(ptB.tiltX * ptB.tiltX + ptB.tiltY * ptB.tiltY),
                    std::atan2(tangents[i + 1].y, tangents[i + 1].x),
                    arcLengths[i + 1] * invTotalLen,
                    0.0f, timeSeconds, ageOf(ptB.timestampSec), tEnd, baseWidth,
                    flows[i + 1], turnAngles[i + 1]
                };

                EvaluatedMods modA = ModulatorEvaluator::EvaluateCoatModulators(coat, ctxA, baseWidth);
                EvaluatedMods modB = ModulatorEvaluator::EvaluateCoatModulators(coat, ctxB, baseWidth);

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
                ColorRgba colA = ModulatorEvaluator::ComputeCoatColor(coat, baseColor, modA.hueShiftDeg, modA.lightnessShift, alphaA);
                ColorRgba colB = ModulatorEvaluator::ComputeCoatColor(coat, baseColor, modB.hueShiftDeg, modB.lightnessShift, alphaB);

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

                float ptSpeed = speeds[segIdx] + (speeds[segIdx + 1] - speeds[segIdx]) * t;
                float ptFlow  = flows[segIdx] + (flows[segIdx + 1] - flows[segIdx]) * t;

                PointContext ctx{
                    s, totalLen - s, pp, pInteg, ptSpeed, 0.0f,
                    std::atan2(tan.y, tan.x),
                    s * invTotalLen,
                    static_cast<float>(pIdx),
                    timeSeconds,
                    ageOf(ptA.timestampSec + (ptB.timestampSec - ptA.timestampSec) * t),
                    tEnd, baseWidth,
                    ptFlow, turnAngles[segIdx]
                };

                EvaluatedMods mod = ModulatorEvaluator::EvaluateCoatModulators(coat, ctx, baseWidth);

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
                ColorRgba col = ModulatorEvaluator::ComputeCoatColor(coat, baseColor, mod.hueShiftDeg, mod.lightnessShift, alpha);
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
                        const int step = static_cast<int>(ModulatorEvaluator::Hash1D(pIdx, coat.modulators.empty() ? 4242u : coat.modulators[0].seed) * 6.999f);
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

} // namespace Folio
