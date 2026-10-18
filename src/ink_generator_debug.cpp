#include "folio_sketch/ink_generator.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>

namespace {
bool s_debugCaptureEnabled = false;
StrokeDebugInfo s_lastDebugInfo;
}

namespace Folio::InkInternal {
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

void InkGenerator::RenderDebugOverlay(BLContext& ctx,
                                      const StrokeDebugInfo& info,
                                      double zoomScale,
                                      const BLPath* outlinePath) {
    double scale = (zoomScale > 1e-4) ? zoomScale : 1.0;
    double lineWidth = 1.5 / scale;
    double dotRadius = 2.5 / scale;
    double crosshairRadius = 5.0 / scale;

    ctx.save();

    // 1. Render filled path at 30% opacity if provided or stored
    const BLPath* path = outlinePath ? outlinePath : (&info.generatedPath);
    if (path && !path->is_empty()) {
        ctx.set_fill_style(BLRgba32(0x00, 0x00, 0x00, 0x4D)); // 30% black
        ctx.fill_path(*path);
    }

    // 2. Yellow wireframe circles for every modeled point's radius disk
    ctx.set_stroke_style(BLRgba32(0xFF, 0xEA, 0x00, 0xC0)); // Yellow
    ctx.set_stroke_width(lineWidth);
    for (const auto& pt : info.cleanPts) {
        if (pt.r > 0.001f) {
            ctx.stroke_circle(pt.x, pt.y, pt.r);
        }
    }

    // 3. Cyan centerline connecting cleanPts with dots
    if (!info.cleanPts.empty()) {
        BLPath centerPath;
        centerPath.move_to(info.cleanPts[0].x, info.cleanPts[0].y);
        for (size_t i = 1; i < info.cleanPts.size(); ++i) {
            centerPath.line_to(info.cleanPts[i].x, info.cleanPts[i].y);
        }
        ctx.set_stroke_style(BLRgba32(0x00, 0xE5, 0xFF, 0xFF)); // Bright Cyan
        ctx.set_stroke_width(lineWidth * 1.2);
        ctx.stroke_path(centerPath);

        ctx.set_fill_style(BLRgba32(0x00, 0xE5, 0xFF, 0xFF));
        for (const auto& pt : info.cleanPts) {
            ctx.fill_circle(pt.x, pt.y, dotRadius * 0.7);
        }
    }

    // 4. Green polyline for leftRail with vertex dots
    if (!info.leftRail.empty()) {
        BLPath leftPath;
        leftPath.move_to(info.leftRail[0].x, info.leftRail[0].y);
        for (size_t i = 1; i < info.leftRail.size(); ++i) {
            leftPath.line_to(info.leftRail[i].x, info.leftRail[i].y);
        }
        ctx.set_stroke_style(BLRgba32(0x00, 0xE6, 0x76, 0xFF)); // Green
        ctx.set_stroke_width(lineWidth);
        ctx.stroke_path(leftPath);

        ctx.set_fill_style(BLRgba32(0x00, 0xE6, 0x76, 0xFF));
        for (const auto& pt : info.leftRail) {
            ctx.fill_circle(pt.x, pt.y, dotRadius * 0.5);
        }
    }

    // 5. Blue polyline for rightRail with vertex dots
    if (!info.rightRail.empty()) {
        BLPath rightPath;
        rightPath.move_to(info.rightRail[0].x, info.rightRail[0].y);
        for (size_t i = 1; i < info.rightRail.size(); ++i) {
            rightPath.line_to(info.rightRail[i].x, info.rightRail[i].y);
        }
        ctx.set_stroke_style(BLRgba32(0x29, 0x79, 0xFF, 0xFF)); // Blue
        ctx.set_stroke_width(lineWidth);
        ctx.stroke_path(rightPath);

        ctx.set_fill_style(BLRgba32(0x29, 0x79, 0xFF, 0xFF));
        for (const auto& pt : info.rightRail) {
            ctx.fill_circle(pt.x, pt.y, dotRadius * 0.5);
        }
    }

    // 6. Highlight folded / retrograde segments in Magenta
    if (!info.retrogradeIndices.empty()) {
        ctx.set_stroke_style(BLRgba32(0xD5, 0x00, 0xF9, 0xFF)); // Bright Magenta
        ctx.set_stroke_width(lineWidth * 2.5);
        for (size_t idx : info.retrogradeIndices) {
            if (idx > 0 && idx < info.leftRail.size()) {
                ctx.stroke_line(info.leftRail[idx - 1].x, info.leftRail[idx - 1].y,
                                info.leftRail[idx].x,     info.leftRail[idx].y);
            }
            if (idx > 0 && idx < info.rightRail.size()) {
                ctx.stroke_line(info.rightRail[idx - 1].x, info.rightRail[idx - 1].y,
                                info.rightRail[idx].x,     info.rightRail[idx].y);
            }
        }
    }

    // 7. Bright Red thick circles / crosshairs at every self-intersection
    if (!info.selfIntersections.empty()) {
        ctx.set_stroke_style(BLRgba32(0xFF, 0x17, 0x44, 0xFF)); // Bright Red
        ctx.set_stroke_width(lineWidth * 2.0);
        ctx.set_fill_style(BLRgba32(0xFF, 0x17, 0x44, 0x80)); // Semi-transparent Red

        for (const auto& pt : info.selfIntersections) {
            ctx.fill_circle(pt.x, pt.y, crosshairRadius);
            ctx.stroke_circle(pt.x, pt.y, crosshairRadius * 1.5);

            // Crosshair lines
            ctx.stroke_line(pt.x - crosshairRadius * 2.0, pt.y,
                            pt.x + crosshairRadius * 2.0, pt.y);
            ctx.stroke_line(pt.x, pt.y - crosshairRadius * 2.0,
                            pt.x, pt.y + crosshairRadius * 2.0);
        }
    }

    ctx.restore();
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
    s_lastDebugInfo.generatedPath.clear();
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

