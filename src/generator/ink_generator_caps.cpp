#include "ink_generator_internal.hpp"

namespace Folio {
namespace InkInternal {

// =============================================================================
// 1. NIB GEOMETRY & SUPPORT OFFSETS
// =============================================================================

std::vector<Point2D> GetNibVertices(CapType capType, double r) {
    std::vector<Point2D> verts;

    switch (capType) {
        case CapType::Square: {
            // Highlighter nib: narrow horizontal width (hw = 0.45r), tall vertical blade (hh = 1.3r)
            // Aspect ratio ~ 3:1 creates thick horizontal bands and thin vertical strokes.
            double hw = r * 0.45;
            double hh = r * 1.3;
            verts = {
                { -hw, -hh }, // Top-Left corner
                { +hw, -hh }, // Top-Right corner
                { +hw, +hh }, // Bottom-Right corner
                { -hw, +hh }  // Bottom-Left corner
            };
            break;
        }

        case CapType::Chisel: {
            // Calligraphy nib: fixed 45-degree angled diamond blade.
            // Produces hairline strokes when moving parallel to 45°, thick ribbons when moving at 135°.
            constexpr double angle = M_PI * -0.25; // Fixed 45-degree fountain pen slant
            double dx = std::cos(angle) * r * 1.3; // Major axis half-length
            double dy = std::sin(angle) * r * 1.3;
            double nx = -dy * 0.1;                 // Minor axis half-thickness
            double ny =  dx * 0.1;
            verts = {
                { -dx - nx, -dy - ny }, // Rear blade tip
                { +dx - nx, +dy - ny }, // Right blade corner
                { +dx + nx, +dy + ny }, // Front blade tip
                { -dx + nx, -dy + ny }  // Left blade corner
            };
            break;
        }

        case CapType::Round:
        default: {
            // Standard circular nib: sampled with 32 radial vertices for ultra-smooth curves
            constexpr int steps = 32;
            verts.reserve(steps);
            for (int i = 0; i < steps; ++i) {
                double a = (2.0 * M_PI * i) / steps;
                verts.push_back({ std::cos(a) * r, std::sin(a) * r });
            }
            break;
        }
    }
    return verts;
}

void GetRibbonOffsets(CapType capType, double r, double nx, double ny,
                      Point2D& outLeft, Point2D& outRight) {
    if (capType == CapType::Round) {
        // Circle: support points are simply along ± normal
        outLeft  = {  nx * r,  ny * r };
        outRight = { -nx * r, -ny * r };
        return;
    }

    // For Square (highlighter) and Chisel (calligraphy):
    // The ribbon edges attach directly to the extreme corners of the actual nib polygon.
    auto verts = GetNibVertices(capType, r);
    Point2D bestLeft = verts[0];
    Point2D bestRight = verts[0];

    // Tangent vector corresponding to normal (nx, ny): u = (-ny, nx)
    double ux = -ny;
    double uy = nx;

    double maxDot = -1e9;
    double minDot = 1e9;
    double maxDotTan = 1e9;
    double minDotTan = 1e9;

    for (const auto& v : verts) {
        double dot = v.x * nx + v.y * ny;
        double tan = v.x * ux + v.y * uy;

        if (dot > maxDot + 1e-5) {
            maxDot = dot;
            bestLeft = v;
            maxDotTan = tan;
        } else if (std::abs(dot - maxDot) <= 1e-5) {
            // Break tie consistently by picking the rear corner (smallest tan projection)
            if (tan < maxDotTan) {
                bestLeft = v;
                maxDotTan = tan;
            }
        }

        if (dot < minDot - 1e-5) {
            minDot = dot;
            bestRight = v;
            minDotTan = tan;
        } else if (std::abs(dot - minDot) <= 1e-5) {
            // Break tie consistently by picking the rear corner (smallest tan projection)
            if (tan < minDotTan) {
                bestRight = v;
                minDotTan = tan;
            }
        }
    }

    outLeft  = bestLeft;
    outRight = bestRight;
}

// =============================================================================
// 2. CAP TRACERS (Single Tap & Endcaps)
// =============================================================================

void TraceRoundCap(BLPath& path,
                   const StrokePoint& center,
                   const Point2D& startPt,
                   const Point2D& endPt,
                   double ux, double uy) {
    double rStart = std::hypot(startPt.x - center.x, startPt.y - center.y);
    double rEnd   = std::hypot(endPt.x - center.x, endPt.y - center.y);
    if (rStart < 1e-6 && rEnd < 1e-6) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    double aStart = std::atan2(startPt.y - center.y, startPt.x - center.x);
    double aEnd   = std::atan2(endPt.y - center.y, endPt.x - center.x);

    // Two possible arcs between aStart and aEnd
    double sweep1 = aEnd - aStart;
    while (sweep1 < 0.0) sweep1 += 2.0 * M_PI;
    while (sweep1 >= 2.0 * M_PI) sweep1 -= 2.0 * M_PI;

    double sweep2 = sweep1 - 2.0 * M_PI;

    // Check which arc midpoint points in the outward direction (ux, uy)
    double mid1 = aStart + sweep1 * 0.5;
    double dot1 = std::cos(mid1) * ux + std::sin(mid1) * uy;

    double sweep = (dot1 >= 0.0) ? sweep1 : sweep2;
    double absSweep = std::abs(sweep);

    if (absSweep < 1e-4) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    const int arcSteps = std::clamp(static_cast<int>(std::ceil(12.0 * (absSweep / M_PI))), 2, 16);
    for (int i = 1; i < arcSteps; ++i) {
        double t = static_cast<double>(i) / arcSteps;
        double a = aStart + t * sweep;
        double r = rStart + t * (rEnd - rStart);
        path.line_to(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
    }

    path.line_to(endPt.x, endPt.y);
}

void TracePolygonCap(BLPath& path,
                     const StrokePoint& center,
                     const Point2D& startPt,
                     const Point2D& endPt,
                     double ux, double uy,
                     CapType capType) {
    auto verts = GetNibVertices(capType, center.r);
    if (verts.empty()) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    Point2D startRel = { startPt.x - center.x, startPt.y - center.y };
    Point2D endRel   = { endPt.x - center.x,   endPt.y - center.y };

    int startIdx = 0, endIdx = 0;
    double minDStart = 1e9, minDEnd = 1e9;
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        double ds = std::hypot(verts[i].x - startRel.x, verts[i].y - startRel.y);
        double de = std::hypot(verts[i].x - endRel.x,   verts[i].y - endRel.y);
        if (ds < minDStart) { minDStart = ds; startIdx = i; }
        if (de < minDEnd)   { minDEnd = de;   endIdx = i; }
    }

    if (startIdx == endIdx) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    const int K = static_cast<int>(verts.size());

    std::vector<int> pathCCW;
    for (int idx = (startIdx + 1) % K; idx != endIdx; idx = (idx + 1) % K) {
        pathCCW.push_back(idx);
    }

    std::vector<int> pathCW;
    for (int idx = (startIdx - 1 + K) % K; idx != endIdx; idx = (idx - 1 + K) % K) {
        pathCW.push_back(idx);
    }

    auto evalProj = [&](const std::vector<int>& p) -> double {
        if (p.empty()) return -1e9;
        double sum = 0.0;
        for (int idx : p) {
            sum += (verts[idx].x - startRel.x) * ux + (verts[idx].y - startRel.y) * uy;
        }
        return sum / static_cast<double>(p.size());
    };

    double projCCW = evalProj(pathCCW);
    double projCW  = evalProj(pathCW);

    const auto& chosenPath = (projCCW >= projCW) ? pathCCW : pathCW;
    for (int idx : chosenPath) {
        path.line_to(center.x + verts[idx].x, center.y + verts[idx].y);
    }
    path.line_to(endPt.x, endPt.y);
}

} // namespace InkInternal
} // namespace Folio

// =============================================================================
// InkGenerator Cap Methods
// =============================================================================

BLPath InkGenerator::TraceCap(const StrokePoint& pt, CapType capType) {
    BLPath path;
    auto verts = Folio::InkInternal::GetNibVertices(capType, pt.r);
    if (verts.empty()) return path;

    path.move_to(pt.x + verts[0].x, pt.y + verts[0].y);
    for (size_t i = 1; i < verts.size(); ++i) {
        path.line_to(pt.x + verts[i].x, pt.y + verts[i].y);
    }
    path.close();
    return path;
}

void InkGenerator::TraceCap(BLPath& path,
                            const StrokePoint& center,
                            const Point2D& leftPt,
                            const Point2D& rightPt,
                            CapType capType,
                            bool isTail,
                            double dirX,
                            double dirY) {
    const Point2D& startPt = isTail ? leftPt : rightPt;
    const Point2D& endPt   = isTail ? rightPt : leftPt;

    double vx = endPt.x - startPt.x;
    double vy = endPt.y - startPt.y;
    double vLen = std::hypot(vx, vy);

    if (vLen < 1e-7 || center.r < 1e-6) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    // Direction pointing outward from startPt -> endPt chord
    double ux =  vy / vLen;
    double uy = -vx / vLen;

    if ((dirX != 0.0 || dirY != 0.0) && (ux * dirX + uy * dirY < 0.0)) {
        ux = -ux;
        uy = -uy;
    }

    if (capType == CapType::Square || capType == CapType::Chisel) {
        Folio::InkInternal::TracePolygonCap(path, center, startPt, endPt, ux, uy, capType);
    } else {
        Folio::InkInternal::TraceRoundCap(path, center, startPt, endPt, ux, uy);
    }
}
