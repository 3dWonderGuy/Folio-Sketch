#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/geometry/shape_recognizer.hpp
 * @brief Dedicated Geometric Classification, Fitting, and Synthetic Stroke Point Generation
 * =========================================================================================
 *
 * Evaluates coarse RDP corner counts and loop closure gaps to classify and fit:
 * - Straight Lines
 * - Circles
 * - Ellipses
 * - Triangles
 * - Rectangles and Squares
 *
 * Generates clean synthetic StrokePoint arrays preserving base stroke thickness.
 */

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "core/ink_stroke_data.hpp"
#include "core/ink_config.hpp"
#include "engine/post_processing/stroke_post_processor.hpp"

namespace Folio {

/**
 * @enum RecognizedShapeType
 * @brief Categorizes recognized geometric primitive shapes.
 */
enum class RecognizedShapeType : uint8_t {
    None = 0,
    Line,
    Circle,
    Ellipse,
    Triangle,
    Rectangle,
    Square
};

/**
 * @struct ShapeRecognitionResult
 * @brief Diagnostic and geometric result payload of shape classification.
 */
struct ShapeRecognitionResult {
    RecognizedShapeType type = RecognizedShapeType::None;
    bool isClosed = false;
    float confidence = 0.0f;
    std::vector<StrokePoint> syntheticPoints;
};

class ShapeRecognizer {
public:
    /**
     * @brief Classifies an input stroke and generates fitted synthetic points if recognized.
     *
     * @param points Modeled input stroke points.
     * @param config Active inking configuration containing shape snapping thresholds.
     * @return ShapeRecognitionResult containing classification type and fitted synthetic StrokePoint array.
     */
    [[nodiscard]] static ShapeRecognitionResult ClassifyAndFit(
        const std::vector<StrokePoint>& points,
        const InkingConfig& config = InkingConfig{}) {

        ShapeRecognitionResult result;
        if (points.size() < 2) return result;

        const auto& snap = config.shapeSnapping;

        // Compute average stroke radius and arc length
        float avgRadius = 0.0f;
        for (const auto& pt : points) {
            avgRadius += pt.r;
        }
        avgRadius /= static_cast<float>(points.size());

        float totalArcLength = 0.0f;
        for (size_t i = 1; i < points.size(); ++i) {
            totalArcLength += std::hypot(points[i].x - points[i - 1].x,
                                         points[i].y - points[i - 1].y);
        }

        const auto& pStart = points.front();
        const auto& pEnd = points.back();
        float endToEndDist = std::hypot(pEnd.x - pStart.x, pEnd.y - pStart.y);

        // Check closed loop condition
        bool isClosed = (endToEndDist <= snap.loopClosureGapMm) && (totalArcLength > snap.loopClosureGapMm * 2.0f);

        // 1. OPEN STROKE: Test Straight Line
        if (!isClosed) {
            float maxChordDeviation = 0.0f;
            float lineDx = pEnd.x - pStart.x;
            float lineDy = pEnd.y - pStart.y;
            float lineLen = std::hypot(lineDx, lineDy);

            if (lineLen > 1e-4f) {
                float invLineLen = 1.0f / lineLen;
                for (const auto& pt : points) {
                    float dist = std::abs((pt.y - pStart.y) * lineDx - (pt.x - pStart.x) * lineDy) * invLineLen;
                    if (dist > maxChordDeviation) {
                        maxChordDeviation = dist;
                    }
                }
            }

            if (maxChordDeviation <= snap.lineStraightnessToleranceMm) {
                result.type = RecognizedShapeType::Line;
                result.isClosed = false;
                result.confidence = std::clamp(1.0f - (maxChordDeviation / std::max(0.01f, snap.lineStraightnessToleranceMm)), 0.0f, 1.0f);

                // Generate synthetic straight line segment
                constexpr size_t kLineSamples = 16;
                result.syntheticPoints.reserve(kLineSamples);
                for (size_t i = 0; i < kLineSamples; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(kLineSamples - 1);
                    float x = pStart.x + t * lineDx;
                    float y = pStart.y + t * lineDy;
                    result.syntheticPoints.emplace_back(x, y, avgRadius);
                }
                return result;
            }

            return result; // Open non-straight curve
        }

        // 2. CLOSED STROKES: Circle, Ellipse, Triangle, Rectangle, Square
        result.isClosed = true;

        // Compute 2D axis-aligned bounding box and centroid
        double minX = points[0].x, maxX = points[0].x;
        double minY = points[0].y, maxY = points[0].y;
        double cx = 0.0, cy = 0.0;

        for (const auto& pt : points) {
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y);
            maxY = std::max(maxY, pt.y);
            cx += pt.x;
            cy += pt.y;
        }
        cx /= static_cast<double>(points.size());
        cy /= static_cast<double>(points.size());

        float bboxW = static_cast<float>(std::max(0.01, maxX - minX));
        float bboxH = static_cast<float>(std::max(0.01, maxY - minY));
        float radiusA = bboxW * 0.5f;
        float radiusB = bboxH * 0.5f;

        // Perform coarse RDP polygon simplification to count corners
        std::vector<StrokePoint> polygonVertices = StrokePostProcessor::Simplify(
            points,
            /*toleranceMm=*/std::max(0.8f, totalArcLength * 0.04f),
            /*maxTurnDeg=*/snap.polygonCornerAngleThresholdDeg,
            /*radiusTolerance=*/10.0f);

        // Remove redundant duplicate endpoint in closed loop
        if (polygonVertices.size() >= 2) {
            float d = std::hypot(polygonVertices.back().x - polygonVertices.front().x,
                                 polygonVertices.back().y - polygonVertices.front().y);
            if (d < snap.loopClosureGapMm) {
                polygonVertices.pop_back();
            }
        }

        size_t cornerCount = polygonVertices.size();

        // 2a. Triangles (3 corners)
        if (cornerCount == 3) {
            result.type = RecognizedShapeType::Triangle;
            result.confidence = 0.9f;
            SampleClosedPolygon(polygonVertices, avgRadius, result.syntheticPoints);
            return result;
        }

        // 2b. Quadrilaterals: Rectangle or Square (4 corners)
        if (cornerCount == 4) {
            float aspectDiff = std::abs(bboxW - bboxH) / std::max(bboxW, bboxH);
            if (aspectDiff <= snap.circleAspectTolerance) {
                result.type = RecognizedShapeType::Square;
            } else {
                result.type = RecognizedShapeType::Rectangle;
            }
            result.confidence = 0.9f;
            SampleClosedPolygon(polygonVertices, avgRadius, result.syntheticPoints);
            return result;
        }

        // 2c. Curvilinear loops: Circle or Ellipse
        float meanRadialDev = 0.0f;
        for (const auto& pt : points) {
            float dx = static_cast<float>((pt.x - cx) / radiusA);
            float dy = static_cast<float>((pt.y - cy) / radiusB);
            float normalizedDist = std::hypot(dx, dy);
            meanRadialDev += std::abs(normalizedDist - 1.0f);
        }
        meanRadialDev = (meanRadialDev / static_cast<float>(points.size())) * std::min(radiusA, radiusB);

        if (meanRadialDev <= snap.ellipseFitToleranceMm) {
            float aspectDiff = std::abs(bboxW - bboxH) / std::max(bboxW, bboxH);
            if (aspectDiff <= snap.circleAspectTolerance) {
                result.type = RecognizedShapeType::Circle;
                float r = (radiusA + radiusB) * 0.5f;
                SampleEllipse(static_cast<float>(cx), static_cast<float>(cy), r, r, avgRadius, result.syntheticPoints);
            } else {
                result.type = RecognizedShapeType::Ellipse;
                SampleEllipse(static_cast<float>(cx), static_cast<float>(cy), radiusA, radiusB, avgRadius, result.syntheticPoints);
            }
            result.confidence = std::clamp(1.0f - (meanRadialDev / snap.ellipseFitToleranceMm), 0.0f, 1.0f);
            return result;
        }

        return result;
    }

private:
    static void SampleClosedPolygon(const std::vector<StrokePoint>& vertices,
                                    float radius,
                                    std::vector<StrokePoint>& outPoints) {
        if (vertices.size() < 3) return;
        constexpr size_t kSamplesPerEdge = 16;
        size_t n = vertices.size();
        outPoints.reserve(n * kSamplesPerEdge + 1);

        for (size_t i = 0; i < n; ++i) {
            const auto& p0 = vertices[i];
            const auto& p1 = vertices[(i + 1) % n];
            for (size_t s = 0; s < kSamplesPerEdge; ++s) {
                float t = static_cast<float>(s) / static_cast<float>(kSamplesPerEdge);
                outPoints.emplace_back(p0.x + t * (p1.x - p0.x),
                                       p0.y + t * (p1.y - p0.y),
                                       radius);
            }
        }
        // Seamlessly close loop back to first vertex
        outPoints.emplace_back(vertices[0].x, vertices[0].y, radius);
    }

    static void SampleEllipse(float cx, float cy, float rx, float ry,
                              float radius,
                              std::vector<StrokePoint>& outPoints) {
        constexpr size_t kTotalSamples = 64;
        outPoints.reserve(kTotalSamples + 1);
        constexpr float kTwoPi = 6.28318530717958647692f;

        for (size_t i = 0; i <= kTotalSamples; ++i) {
            float theta = (static_cast<float>(i) / static_cast<float>(kTotalSamples)) * kTwoPi;
            float x = cx + rx * std::cos(theta);
            float y = cy + ry * std::sin(theta);
            outPoints.emplace_back(x, y, radius);
        }
    }
};

} // namespace Folio
