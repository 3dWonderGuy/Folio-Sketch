#include "core/ink_stroke_data.hpp"
#include "core/stroke_collision.hpp"
#include "engine/post_processing/shape_recognizer.hpp"
#include "engine/post_processing/stroke_post_processor.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[Folio-Sketch Tests] Starting regression test suite..." << std::endl;

    // Test 1: ColorRgba and StrokePoint primitives
    {
        Folio::ColorRgba color(255, 0, 0, 255);
        assert(color.r == 255 && color.g == 0 && color.b == 0 && color.a == 255);

        Folio::StrokePoint pt(10.0, 20.0, 1.5f, 0.8f);
        assert(pt.x == 10.0 && pt.y == 20.0 && pt.r == 1.5f);

        Folio::Point2D p2d = pt;
        assert(p2d.x == 10.0 && p2d.y == 20.0);
        std::cout << "  ✓ Test 1 Passed: Core primitives and conversions intact." << std::endl;
    }

    // Test 2: Shape Recognizer Line Fit Test
    {
        std::vector<Folio::StrokePoint> straightLine;
        for (int i = 0; i <= 20; ++i) {
            straightLine.emplace_back(static_cast<double>(i * 5), 0.0, 1.0f);
        }

        Folio::InkingConfig config;
        auto result = Folio::ShapeRecognizer::ClassifyAndFit(straightLine, config);
        assert(result.type == Folio::RecognizedShapeType::Line);
        assert(!result.syntheticPoints.empty());
        std::cout << "  ✓ Test 2 Passed: Shape Recognizer correctly identified straight line." << std::endl;
    }

    // Test 3: Shape Recognizer Circle Fit Test
    {
        std::vector<Folio::StrokePoint> circlePoints;
        const double radius = 50.0;
        const double cx = 100.0;
        const double cy = 100.0;
        for (int i = 0; i < 36; ++i) {
            double angle = static_cast<double>(i) * (2.0 * 3.141592653589793 / 36.0);
            circlePoints.emplace_back(cx + radius * std::cos(angle), cy + radius * std::sin(angle), 1.0f);
        }
        // Close loop
        circlePoints.push_back(circlePoints.front());

        Folio::InkingConfig config;
        auto result = Folio::ShapeRecognizer::ClassifyAndFit(circlePoints, config);
        assert(result.type == Folio::RecognizedShapeType::Circle || result.type == Folio::RecognizedShapeType::Ellipse);
        assert(result.isClosed);
        std::cout << "  ✓ Test 3 Passed: Shape Recognizer correctly identified circle." << std::endl;
    }

    // Test 4: Stroke Collision Engine Tests
    {
        std::vector<Folio::Segment1D> segments;
        Folio::Segment1D seg;
        seg.p0 = Folio::Point2D{ 0.0, 0.0 };
        seg.p1 = Folio::Point2D{ 100.0, 0.0 };
        seg.width = 2.0f;
        segments.push_back(seg);

        // Hit test directly on the line at (50, 0)
        auto hit = Folio::StrokeCollisionEngine::HitTestStroke(segments, 50.0, 0.0, 1.0);
        assert(hit.hit);

        // Hit test far away from the line at (50, 50)
        auto miss = Folio::StrokeCollisionEngine::HitTestStroke(segments, 50.0, 50.0, 1.0);
        assert(!miss.hit);
        std::cout << "  ✓ Test 4 Passed: Stroke collision engine hit-testing verified." << std::endl;
    }

    std::cout << "[Folio-Sketch Tests] All regression tests passed successfully!" << std::endl;
    return 0;
}
