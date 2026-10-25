#include "folio_sketch/ink_generator.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>

#ifdef FOLIO_ENABLE_THORSVG
#include <thorvg.h>
#endif

namespace Folio {

namespace {
bool s_debugCaptureEnabled = false;
StrokeDebugInfo s_lastDebugInfo;
}

namespace InkInternal {
StrokeDebugInfo& GetMutableDebugInfo() noexcept {
    return s_lastDebugInfo;
}
}

void InkGenerator::SetDebugCapture(bool enabled) noexcept {
    s_debugCaptureEnabled = enabled;
}

bool InkGenerator::IsDebugCaptureEnabled() noexcept {
    return s_debugCaptureEnabled;
}

const StrokeDebugInfo& InkGenerator::GetLastDebugInfo() noexcept {
    return s_lastDebugInfo;
}

static void FormatStrokeDump(std::ostream& os, const StrokeDebugInfo& info) {
    os << "// Auto-dumped stroke (Self-intersections: " << info.selfIntersections.size()
       << ", Retrograde folds: " << info.retrogradeIndices.size()
       << ", Points: " << info.rawInputPts.size() << ")\n";
    os << "std::vector<StrokePoint> reproPts = {\n";
    os << std::fixed << std::setprecision(4);

    for (size_t i = 0; i < info.rawInputPts.size(); ++i) {
        const auto& p = info.rawInputPts[i];
        os << "    { " << p.x << ", " << p.y << ", "
           << p.r << "f, " << p.pressure << "f, "
           << p.tiltX << "f, " << p.tiltY << "f }";
        if (i + 1 < info.rawInputPts.size()) os << ",";
        os << "\n";
    }
    os << "};\n";
}

void InkGenerator::ClearDebugInfo() noexcept {
    s_lastDebugInfo.rawInputPts.clear();
    s_lastDebugInfo.cleanPts.clear();
    s_lastDebugInfo.leftRail.clear();
    s_lastDebugInfo.rightRail.clear();
    s_lastDebugInfo.selfIntersections.clear();
    s_lastDebugInfo.retrogradeIndices.clear();
    s_lastDebugInfo.hasDefects = false;
    s_lastDebugInfo.builtPath.clear();
}

void InkGenerator::DumpLastStrokeToConsole(const StrokeDebugInfo* info) {
    FormatStrokeDump(std::cout, info ? *info : s_lastDebugInfo);
}

void InkGenerator::DumpStrokeToFile(const std::string& filepath, const StrokeDebugInfo* info) {
    std::ofstream ofs(filepath, std::ios::app);
    if (ofs.is_open()) {
        FormatStrokeDump(ofs, info ? *info : s_lastDebugInfo);
    }
}

bool InkGenerator::DumpDebugSvg(const std::string& filepath, const StrokeDebugInfo* infoPtr) {
    const auto& info = infoPtr ? *infoPtr : s_lastDebugInfo;
    if (info.cleanPts.empty() && info.rawInputPts.empty() && info.builtPath.empty()) {
        return false;
    }

    // Determine bounding box for SVG viewBox
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    auto expand = [&](double x, double y) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
    };

    for (const auto& p : info.cleanPts) {
        expand(p.x - p.r, p.y - p.r);
        expand(p.x + p.r, p.y + p.r);
    }
    for (const auto& p : info.leftRail) expand(p.x, p.y);
    for (const auto& p : info.rightRail) expand(p.x, p.y);

    if (minX > maxX) {
        minX = 0; minY = 0; maxX = 100; maxY = 100;
    }

    // Add 10% padding
    double pad = std::max(5.0, std::max(maxX - minX, maxY - minY) * 0.1);
    minX -= pad; minY -= pad; maxX += pad; maxY += pad;
    double width = maxX - minX;
    double height = maxY - minY;

    std::ofstream svg(filepath);
    if (!svg.is_open()) return false;

    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
        << minX << " " << minY << " " << width << " " << height
        << "\" width=\"100%\" height=\"100%\" style=\"background: #1e1e1e;\">\n";

    // Layer 1: Filled Stroke Polygon (Dark Red, 40% opacity)
    if (!info.builtPath.empty()) {
        svg << "  <!-- Built Stroke Contour -->\n";
        svg << "  <path d=\"";
        const auto& verbs = info.builtPath.verbs();
        const auto& points = info.builtPath.points();
        size_t pIdx = 0;
        for (auto verb : verbs) {
            switch (verb) {
                case PathVerb::MoveTo:
                    if (pIdx < points.size()) {
                        svg << "M " << points[pIdx].x << " " << points[pIdx].y << " ";
                        ++pIdx;
                    }
                    break;
                case PathVerb::LineTo:
                    if (pIdx < points.size()) {
                        svg << "L " << points[pIdx].x << " " << points[pIdx].y << " ";
                        ++pIdx;
                    }
                    break;
                case PathVerb::Close:
                    svg << "Z ";
                    break;
                default:
                    break;
            }
        }
        svg << "\" fill=\"#e53935\" fill-opacity=\"0.35\" stroke=\"#e53935\" stroke-width=\"0.5\" stroke-opacity=\"0.6\" fill-rule=\"evenodd\"/>\n";
    }

    // Layer 2: Yellow radius circles at every clean centerline vertex
    svg << "  <!-- Radius Wireframes -->\n";
    for (const auto& p : info.cleanPts) {
        if (p.r > 0.001f) {
            svg << "  <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"" << p.r
                << "\" fill=\"none\" stroke=\"#ffeb3b\" stroke-width=\"0.3\" stroke-dasharray=\"1,1\" stroke-opacity=\"0.4\"/>\n";
        }
    }

    // Layer 3: Left Rail (Green polyline)
    if (!info.leftRail.empty()) {
        svg << "  <!-- Left Rail (Green) -->\n";
        svg << "  <polyline points=\"";
        for (const auto& p : info.leftRail) {
            svg << p.x << "," << p.y << " ";
        }
        svg << "\" fill=\"none\" stroke=\"#00e676\" stroke-width=\"0.6\" stroke-opacity=\"0.9\"/>\n";
        for (const auto& p : info.leftRail) {
            svg << "  <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"0.4\" fill=\"#00e676\"/>\n";
        }
    }

    // Layer 4: Right Rail (Blue polyline)
    if (!info.rightRail.empty()) {
        svg << "  <!-- Right Rail (Blue) -->\n";
        svg << "  <polyline points=\"";
        for (const auto& p : info.rightRail) {
            svg << p.x << "," << p.y << " ";
        }
        svg << "\" fill=\"none\" stroke=\"#2979ff\" stroke-width=\"0.6\" stroke-opacity=\"0.9\"/>\n";
        for (const auto& p : info.rightRail) {
            svg << "  <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"0.4\" fill=\"#2979ff\"/>\n";
        }
    }

    // Layer 5: Cyan Centerline with vertex nodes
    if (!info.cleanPts.empty()) {
        svg << "  <!-- Centerline (Cyan) -->\n";
        svg << "  <polyline points=\"";
        for (const auto& p : info.cleanPts) {
            svg << p.x << "," << p.y << " ";
        }
        svg << "\" fill=\"none\" stroke=\"#00e5ff\" stroke-width=\"0.8\"/>\n";
        for (size_t i = 0; i < info.cleanPts.size(); ++i) {
            const auto& p = info.cleanPts[i];
            svg << "  <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"0.5\" fill=\"#00e5ff\"/>\n";
        }
    }

    // Layer 6: Self-Intersections (Red Crosshairs)
    if (!info.selfIntersections.empty()) {
        svg << "  <!-- Self-Intersections (Red Crosshairs) -->\n";
        for (const auto& p : info.selfIntersections) {
            svg << "  <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"2.0\" fill=\"none\" stroke=\"#ff1744\" stroke-width=\"0.8\"/>\n";
            svg << "  <line x1=\"" << p.x - 3 << "\" y1=\"" << p.y << "\" x2=\"" << p.x + 3 << "\" y2=\"" << p.y << "\" stroke=\"#ff1744\" stroke-width=\"0.8\"/>\n";
            svg << "  <line x1=\"" << p.x << "\" y1=\"" << p.y - 3 << "\" x2=\"" << p.x << "\" y2=\"" << p.y + 3 << "\" stroke=\"#ff1744\" stroke-width=\"0.8\"/>\n";
        }
    }

    // Layer 7: Retrograde Folds (Magenta segments)
    if (!info.retrogradeIndices.empty()) {
        svg << "  <!-- Retrograde Folds (Magenta) -->\n";
        for (size_t idx : info.retrogradeIndices) {
            if (idx > 0 && idx < info.leftRail.size()) {
                svg << "  <line x1=\"" << info.leftRail[idx - 1].x << "\" y1=\"" << info.leftRail[idx - 1].y
                    << "\" x2=\"" << info.leftRail[idx].x << "\" y2=\"" << info.leftRail[idx].y
                    << "\" stroke=\"#d500f9\" stroke-width=\"1.5\"/>\n";
            }
            if (idx > 0 && idx < info.rightRail.size()) {
                svg << "  <line x1=\"" << info.rightRail[idx - 1].x << "\" y1=\"" << info.rightRail[idx - 1].y
                    << "\" x2=\"" << info.rightRail[idx].x << "\" y2=\"" << info.rightRail[idx].y
                    << "\" stroke=\"#d500f9\" stroke-width=\"1.5\"/>\n";
            }
        }
    }

    svg << "</svg>\n";
    return true;
}

} // namespace Folio

