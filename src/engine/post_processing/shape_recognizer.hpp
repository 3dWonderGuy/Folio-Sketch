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
 * Implementations live in shape_recognizer.cpp.
 */

#include <vector>
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
    Square,
    Diamond,
    Quadrilateral // Trapezoid / Parallelogram / irregular 4-gon
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
        const InkingConfig& config = InkingConfig{});

private:
    static void SampleClosedPolygon(const std::vector<StrokePoint>& vertices,
                                    float radius,
                                    std::vector<StrokePoint>& outPoints);

    static void SampleEllipse(float cx, float cy, float rx, float ry,
                              float angleRad,
                              float radius,
                              std::vector<StrokePoint>& outPoints);
};

} // namespace Folio
