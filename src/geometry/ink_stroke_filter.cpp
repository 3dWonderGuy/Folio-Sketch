#include "ink_stroke_filter.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {
namespace InkInternal {

std::vector<StrokePoint> StrokeFilter::PreFilterPoints(std::span<const StrokePoint> pts,
                                                       bool stationaryEnvelopment) {
    std::vector<StrokePoint> cleanPts;
    if (pts.empty()) return cleanPts;

    cleanPts.reserve(pts.size());

    for (size_t idx = 0; idx < pts.size(); ++idx) {
        const auto& p = pts[idx];
        if (cleanPts.empty()) {
            cleanPts.push_back(p);
            continue;
        }

        auto& prev = cleanPts.back();
        double d = std::hypot(p.x - prev.x, p.y - prev.y);
        double maxR = std::max(static_cast<double>(p.r), static_cast<double>(prev.r));

        if (stationaryEnvelopment) {
            // 1a. Disk containment: new point engulfed by previous kept point
            if (d + p.r <= prev.r + maxR * 0.05) {
                prev.pressure = std::max(prev.pressure, p.pressure);
                prev.r = std::max(prev.r, p.r);
                continue;
            }

            // 1b. Disk containment: previous kept point engulfed by new point's larger disk
            if (d + prev.r <= p.r + maxR * 0.05) {
                if (cleanPts.size() == 1) {
                    prev = p;
                } else {
                    cleanPts.back() = p;
                }
                continue;
            }
        } else if (d < 1e-5) {
            prev.pressure = std::max(prev.pressure, p.pressure);
            prev.r = std::max(prev.r, p.r);
            continue;
        }

        // 1c. Radius-proportional minimum step for interior points
        bool isLast = (idx + 1 == pts.size());
        double minStep = std::max(0.05, maxR * 0.20);
        if (d < minStep) {
            bool isSharpTurn = false;
            if (!isLast && idx + 1 < pts.size() && d > 1e-5) {
                double dxIn = p.x - prev.x, dyIn = p.y - prev.y;
                double dxOut = pts[idx + 1].x - p.x, dyOut = pts[idx + 1].y - p.y;
                double lenOut = std::hypot(dxOut, dyOut);
                if (lenOut > 1e-5) {
                    double cosTurn = (dxIn * dxOut + dyIn * dyOut) / (d * lenOut);
                    if (cosTurn < 0.70 && d >= maxR * 0.08) {
                        isSharpTurn = true;
                    }
                }
            }
            if (!isSharpTurn) {
                if (isLast) {
                    if (cleanPts.size() == 1) {
                        prev = p;
                    } else {
                        // Check if final segment continues forward along the stroke tangent
                        double dxIn = 0.0, dyIn = 0.0;
                        if (cleanPts.size() >= 2) {
                            dxIn = prev.x - cleanPts[cleanPts.size() - 2].x;
                            dyIn = prev.y - cleanPts[cleanPts.size() - 2].y;
                        }
                        double dxOut = p.x - prev.x;
                        double dyOut = p.y - prev.y;
                        if (dxIn * dxOut + dyIn * dyOut > 0.0) {
                            // Forward continuation: preserve final pen-lift coordinate
                            prev.x = p.x;
                            prev.y = p.y;
                        }
                        prev.pressure = std::max(prev.pressure, p.pressure);
                        prev.r = std::max(prev.r, p.r);
                    }
                } else {
                    prev.pressure = std::max(prev.pressure, p.pressure);
                    prev.r = std::max(prev.r, p.r);
                }
                continue;
            }
        }

        cleanPts.push_back(p);
    }

    // Remove terminal micro-jitter stub if last segment is smaller than 20% of radius and retrograde
    while (cleanPts.size() >= 3) {
        double dxIn = cleanPts[cleanPts.size() - 2].x - cleanPts[cleanPts.size() - 3].x;
        double dyIn = cleanPts[cleanPts.size() - 2].y - cleanPts[cleanPts.size() - 3].y;
        double dxOut = cleanPts.back().x - cleanPts[cleanPts.size() - 2].x;
        double dyOut = cleanPts.back().y - cleanPts[cleanPts.size() - 2].y;
        double dLast = std::hypot(dxOut, dyOut);
        if (dLast < cleanPts.back().r * 0.20 && (dxIn * dxOut + dyIn * dyOut <= 0.0)) {
            cleanPts.pop_back();
        } else {
            break;
        }
    }

    return cleanPts;
}

void StrokeFilter::RegularizeRadiusSlope(std::vector<StrokePoint>& pts, double maxSlope) {
    if (pts.size() < 2) return;

    // Forward pass
    for (size_t i = 1; i < pts.size(); ++i) {
        double d = std::hypot(pts[i].x - pts[i - 1].x,
                              pts[i].y - pts[i - 1].y);
        double maxAllowedR = pts[i - 1].r + d * maxSlope;
        if (pts[i].r > maxAllowedR) {
            pts[i].r = static_cast<float>(maxAllowedR);
        }
    }

    // Backward pass
    for (size_t i = pts.size() - 1; i > 0; --i) {
        double d = std::hypot(pts[i - 1].x - pts[i].x,
                              pts[i - 1].y - pts[i].y);
        double maxAllowedR = pts[i].r + d * maxSlope;
        if (pts[i - 1].r > maxAllowedR) {
            pts[i - 1].r = static_cast<float>(maxAllowedR);
        }
    }
}

std::vector<size_t> StrokeFilter::FindHairpinTurns(std::span<const StrokePoint> pts, double cosThreshold) {
    std::vector<size_t> splitIndices;
    if (pts.size() < 3) return splitIndices;

    for (size_t i = 1; i < pts.size() - 1; ++i) {
        double dxIn  = pts[i].x - pts[i - 1].x;
        double dyIn  = pts[i].y - pts[i - 1].y;
        double lenIn = std::hypot(dxIn, dyIn);

        double dxOut  = pts[i + 1].x - pts[i].x;
        double dyOut  = pts[i + 1].y - pts[i].y;
        double lenOut = std::hypot(dxOut, dyOut);

        if (lenIn > 1e-7 && lenOut > 1e-7) {
            double cosTheta = (dxIn * dxOut + dyIn * dyOut) / (lenIn * lenOut);
            if (cosTheta < cosThreshold) {
                splitIndices.push_back(i);
            }
        }
    }

    return splitIndices;
}

} // namespace InkInternal
} // namespace Folio
