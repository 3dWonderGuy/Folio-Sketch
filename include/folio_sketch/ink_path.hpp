#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/ink_path.hpp
 * @brief High-Performance 2D Vector Path Definition & ThorSVG Bridge
 * =========================================================================================
 */

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
#include <memory>
#include <cstdio>

namespace tvg {
class Shape;
}

namespace Folio {

/**
 * @struct PathPoint
 * @brief Lightweight 2D float point used for vertex streams in vector paths.
 */
struct PathPoint {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * @struct BoundingBox2D
 * @brief Axis-aligned 2D bounding box [x0, y0, x1, y1] in world coordinates.
 */
struct BoundingBox2D {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;

    constexpr BoundingBox2D() noexcept = default;
    constexpr BoundingBox2D(double x0_, double y0_, double x1_, double y1_) noexcept
        : x0(x0_), y0(y0_), x1(x1_), y1(y1_) {}

    [[nodiscard]] constexpr double width() const noexcept { return x1 - x0; }
    [[nodiscard]] constexpr double height() const noexcept { return y1 - y0; }
    [[nodiscard]] constexpr bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

/**
 * @enum PathVerb
 * @brief Drawing commands that form a 2D vector path.
 */
enum class PathVerb : uint8_t {
    MoveTo = 0,
    LineTo,
    CubicTo,
    Close
};

/**
 * @class InkPath
 * @brief Vector path representation for stroke outlines, compatible with ThorSVG.
 */
class InkPath {
public:
    struct PathView {
        size_t size = 0;
        const PathPoint* vertex_data = nullptr;
    };

    InkPath() = default;

    void moveTo(double x, double y) {
        m_verbs.push_back(PathVerb::MoveTo);
        m_points.push_back({static_cast<float>(x), static_cast<float>(y)});
        updateBounds(x, y);
    }

    void lineTo(double x, double y) {
        m_verbs.push_back(PathVerb::LineTo);
        m_points.push_back({static_cast<float>(x), static_cast<float>(y)});
        updateBounds(x, y);
    }

    void cubicTo(double cx1, double cy1, double cx2, double cy2, double x, double y) {
        m_verbs.push_back(PathVerb::CubicTo);
        m_points.push_back({static_cast<float>(cx1), static_cast<float>(cy1)});
        m_points.push_back({static_cast<float>(cx2), static_cast<float>(cy2)});
        m_points.push_back({static_cast<float>(x), static_cast<float>(y)});
        updateBounds(cx1, cy1);
        updateBounds(cx2, cy2);
        updateBounds(x, y);
    }

    void close() {
        m_verbs.push_back(PathVerb::Close);
    }

    // Snake_case aliases for compatibility
    void move_to(double x, double y) { moveTo(x, y); }
    void line_to(double x, double y) { lineTo(x, y); }
    void cubic_to(double cx1, double cy1, double cx2, double cy2, double x, double y) { cubicTo(cx1, cy1, cx2, cy2, x, y); }

    void addCircle(double cx, double cy, double r) {
        constexpr double k = 0.5522847498307936;
        const double kr = k * r;
        moveTo(cx + r, cy);
        cubicTo(cx + r, cy + kr, cx + kr, cy + r, cx, cy + r);
        cubicTo(cx - kr, cy + r, cx - r, cy + kr, cx - r, cy);
        cubicTo(cx - r, cy - kr, cx - kr, cy - r, cx, cy - r);
        cubicTo(cx + kr, cy - r, cx + r, cy - kr, cx + r, cy);
        close();
    }

    void add_circle(double cx, double cy, double r) { addCircle(cx, cy, r); }

    void addPath(const InkPath& other) {
        if (other.empty()) return;
        m_verbs.insert(m_verbs.end(), other.m_verbs.begin(), other.m_verbs.end());
        m_points.insert(m_points.end(), other.m_points.begin(), other.m_points.end());
        if (other.m_hasBounds) {
            updateBounds(other.m_bounds.x0, other.m_bounds.y0);
            updateBounds(other.m_bounds.x1, other.m_bounds.y1);
        }
    }

    void add_path(const InkPath& other) { addPath(other); }

    void clear() noexcept {
        m_verbs.clear();
        m_points.clear();
        m_bounds = BoundingBox2D{};
        m_hasBounds = false;
    }

    [[nodiscard]] bool empty() const noexcept { return m_verbs.empty(); }
    [[nodiscard]] bool is_empty() const noexcept { return empty(); }

    [[nodiscard]] const std::vector<PathVerb>& verbs() const noexcept { return m_verbs; }
    [[nodiscard]] const std::vector<PathPoint>& points() const noexcept { return m_points; }

    [[nodiscard]] PathView view() const noexcept {
        return PathView{m_points.size(), m_points.data()};
    }

    [[nodiscard]] const BoundingBox2D& bounds() const noexcept { return m_bounds; }
    [[nodiscard]] BoundingBox2D getBounds() const noexcept { return m_bounds; }
    bool get_bounding_box(BoundingBox2D* out) const noexcept {
        if (out) *out = m_bounds;
        return m_hasBounds;
    }

    /**
     * @brief Exports path commands to any generic 2D path object (e.g. Blend2D BLPath)
     *        without requiring host library headers.
     */
    template <typename TPath>
    void exportTo(TPath& dst) const {
        size_t ptIdx = 0;
        for (PathVerb verb : m_verbs) {
            switch (verb) {
                case PathVerb::MoveTo:
                    if (ptIdx < m_points.size()) {
                        dst.moveTo(m_points[ptIdx].x, m_points[ptIdx].y);
                        ptIdx++;
                    }
                    break;
                case PathVerb::LineTo:
                    if (ptIdx < m_points.size()) {
                        dst.lineTo(m_points[ptIdx].x, m_points[ptIdx].y);
                        ptIdx++;
                    }
                    break;
                case PathVerb::CubicTo:
                    if (ptIdx + 2 < m_points.size()) {
                        dst.cubicTo(m_points[ptIdx].x, m_points[ptIdx].y,
                                    m_points[ptIdx + 1].x, m_points[ptIdx + 1].y,
                                    m_points[ptIdx + 2].x, m_points[ptIdx + 2].y);
                        ptIdx += 3;
                    }
                    break;
                case PathVerb::Close:
                    dst.close();
                    break;
            }
        }
    }

    template <typename TPath>
    void toBLPath(TPath& dst) const {
        exportTo(dst);
    }

    /**
     * @brief Converts this InkPath to a ThorSVG Shape for vector rendering.
     *        Templated so <thorvg.h> is only required if the caller explicitly invokes it.
     */
    template <typename TShape = tvg::Shape>
    [[nodiscard]] TShape* toThorVGShape() const {
        auto* shape = TShape::gen();
        if (m_verbs.empty()) return shape;

        size_t ptIdx = 0;
        for (PathVerb verb : m_verbs) {
            switch (verb) {
                case PathVerb::MoveTo:
                    if (ptIdx < m_points.size()) {
                        shape->moveTo(m_points[ptIdx].x, m_points[ptIdx].y);
                        ptIdx++;
                    }
                    break;
                case PathVerb::LineTo:
                    if (ptIdx < m_points.size()) {
                        shape->lineTo(m_points[ptIdx].x, m_points[ptIdx].y);
                        ptIdx++;
                    }
                    break;
                case PathVerb::CubicTo:
                    if (ptIdx + 2 < m_points.size()) {
                        shape->cubicTo(m_points[ptIdx].x, m_points[ptIdx].y,
                                       m_points[ptIdx + 1].x, m_points[ptIdx + 1].y,
                                       m_points[ptIdx + 2].x, m_points[ptIdx + 2].y);
                        ptIdx += 3;
                    }
                    break;
                case PathVerb::Close:
                    shape->close();
                    break;
            }
        }
        return shape;
    }

    [[nodiscard]] std::string toSvgPathData() const {
        return toSvgPath();
    }

    /**
     * @brief Exports path commands to standard SVG 'd' attribute string format.
     */
    [[nodiscard]] std::string toSvgPath() const {
        std::string s;
        s.reserve(m_verbs.size() * 24);
        char buf[128];
        size_t ptIdx = 0;

        for (PathVerb verb : m_verbs) {
            switch (verb) {
                case PathVerb::MoveTo:
                    if (ptIdx < m_points.size()) {
                        snprintf(buf, sizeof(buf), "M%.2f,%.2f ", m_points[ptIdx].x, m_points[ptIdx].y);
                        s += buf;
                        ptIdx++;
                    }
                    break;
                case PathVerb::LineTo:
                    if (ptIdx < m_points.size()) {
                        snprintf(buf, sizeof(buf), "L%.2f,%.2f ", m_points[ptIdx].x, m_points[ptIdx].y);
                        s += buf;
                        ptIdx++;
                    }
                    break;
                case PathVerb::CubicTo:
                    if (ptIdx + 2 < m_points.size()) {
                        snprintf(buf, sizeof(buf), "C%.2f,%.2f %.2f,%.2f %.2f,%.2f ",
                                 m_points[ptIdx].x, m_points[ptIdx].y,
                                 m_points[ptIdx + 1].x, m_points[ptIdx + 1].y,
                                 m_points[ptIdx + 2].x, m_points[ptIdx + 2].y);
                        s += buf;
                        ptIdx += 3;
                    }
                    break;
                case PathVerb::Close:
                    s += "Z ";
                    break;
            }
        }
        return s;
    }

private:
    void updateBounds(double x, double y) {
        if (!m_hasBounds) {
            m_bounds = BoundingBox2D{x, y, x, y};
            m_hasBounds = true;
        } else {
            m_bounds.x0 = std::min(m_bounds.x0, x);
            m_bounds.y0 = std::min(m_bounds.y0, y);
            m_bounds.x1 = std::max(m_bounds.x1, x);
            m_bounds.y1 = std::max(m_bounds.y1, y);
        }
    }

    std::vector<PathVerb> m_verbs;
    std::vector<PathPoint> m_points;
    BoundingBox2D m_bounds{};
    bool m_hasBounds = false;
};

} // namespace Folio

using InkPath = Folio::InkPath;
using PathVerb = Folio::PathVerb;

#if !defined(BLEND2D_H) && !defined(BL_PATH_H)
using BLPath = Folio::InkPath;
using BLBox = Folio::BoundingBox2D;
using BLPathView = Folio::InkPath::PathView;
#endif
