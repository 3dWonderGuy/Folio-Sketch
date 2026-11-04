/**
 * =========================================================================================
 * @file src/engine/ink_stroke_finisher.cpp
 * @brief Stroke Finalization, Edge-Case Handling, and Collision Packaging
 * =========================================================================================
 */

#include "ink_stroke_finisher.hpp"
#include <algorithm>
#include <cmath>

namespace Folio {

void InkStrokeFinisher::ResolveSingleTapEdgeCase(
    const std::vector<StylusInputPoint>& rawInputs,
    float tapWidth,
    std::vector<StrokePoint>& inoutModeledPoints)
{
    if (rawInputs.empty()) return;

    double strokeLen = 0.0;
    for (size_t i = 1; i < inoutModeledPoints.size(); ++i) {
        strokeLen += std::hypot(inoutModeledPoints[i].x - inoutModeledPoints[i - 1].x,
                                inoutModeledPoints[i].y - inoutModeledPoints[i - 1].y);
        if (strokeLen >= 1e-4) {
            break;
        }
    }

    if (inoutModeledPoints.empty()) {
        // Tremor filter swallowed the stationary tap: synthesize a dot at touchdown origin
        const auto& pt = rawInputs.front();
        float tapPressure = (pt.pressure >= 0.0f) ? pt.pressure : 0.65f;
        inoutModeledPoints.push_back(StrokePoint(pt.x, pt.y, tapWidth * 0.5f, tapPressure, pt.tiltX, pt.tiltY));
    } else if (strokeLen < 1e-4) {
        // Modeler flushed multiple coincident points for a stationary tap; collapse to a single point
        inoutModeledPoints.resize(1);
    }
}

void InkStrokeFinisher::ApplyTailTaper(
    const InkingConfig& config,
    const StrokeStyle& style,
    std::vector<StrokePoint>& inoutModeledPoints)
{
    if (config.dynamics.tailTaperLengthMm <= 0.0f || inoutModeledPoints.size() < 2) return;

    float minRadius = style.baseWidth * std::min(config.dynamics.minWidthMultiplier, config.dynamics.maxWidthMultiplier) * 0.5f;
    minRadius = std::max(0.001f, minRadius);
    const double taperTargetLen = static_cast<double>(config.dynamics.tailTaperLengthMm);
    const float taperPower = (config.dynamics.tailTaperPower > 0.01f) ? config.dynamics.tailTaperPower : 1.0f;

    const size_t ptCount = inoutModeledPoints.size();
    double d = 0.0;
    float origTipR = inoutModeledPoints.back().r;
    inoutModeledPoints.back().r = std::min(origTipR, minRadius);
    inoutModeledPoints.back().pressure = 0.0f;

    for (size_t i = ptCount - 1; i > 0; --i) {
        double segLen = std::hypot(inoutModeledPoints[i].x - inoutModeledPoints[i - 1].x,
                                   inoutModeledPoints[i].y - inoutModeledPoints[i - 1].y);
        d += segLen;
        if (d >= taperTargetLen) {
            break;
        }
        float u = static_cast<float>(std::clamp(d / taperTargetLen, 0.0, 1.0));
        float factor = (taperPower == 1.0f) ? u : std::pow(u, taperPower);
        auto& pt = inoutModeledPoints[i - 1];
        if (pt.r > minRadius) {
            pt.r = minRadius + (pt.r - minRadius) * factor;
        }
        pt.pressure *= factor;
    }
}

std::vector<Segment1D> InkStrokeFinisher::BuildCollisionSegments(
    const std::vector<StrokePoint>& modeledPoints)
{
    std::vector<Segment1D> segments;
    if (modeledPoints.size() == 1) {
        // Single-point tap: create a micro-segment because zero-length lines have undefined normals
        const auto& p0 = modeledPoints[0];
        Point2D pt1(p0.x + 0.001, p0.y);
        Segment1D seg;
        seg.p0 = p0;
        seg.p1 = pt1;
        seg.width = p0.r * 2.0f;
        segments.push_back(seg);
    } else if (modeledPoints.size() >= 2) {
        // Multi-point stroke: link each consecutive pair of modeled points into a collision capsule
        segments.reserve(modeledPoints.size() - 1);
        for (size_t i = 1; i < modeledPoints.size(); ++i) {
            Segment1D seg;
            seg.p0 = modeledPoints[i - 1];
            seg.p1 = modeledPoints[i];
            seg.width = modeledPoints[i].r * 2.0f;
            segments.push_back(seg);
        }
    }
    return segments;
}

} // namespace Folio
