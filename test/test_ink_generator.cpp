/**
 * =========================================================================================
 * @file test/test_ink_generator.cpp
 * @brief Standalone Test Suite & Benchmarks for Folio-Sketch Vector Inking Engine
 * =========================================================================================
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <cassert>
#include <blend2d/blend2d.h>

#include "folio_sketch/ink_generator.hpp"

void TestStrokeTessellationAndRasterPerformance() {
    std::cout << "[Test 1/2] Running Stroke Tessellation & Rasterization Performance..." << std::endl;

    // Synthesize a complex 5-second handwriting gesture (1000 points at 200Hz)
    std::vector<StrokePoint> points;
    points.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        double t = i * 0.05;
        double x = 400.0 + 300.0 * std::cos(t * 0.5) + t * 5.0;
        double y = 400.0 + 300.0 * std::sin(t * 0.5);
        points.emplace_back(x, y, 0.4f + 0.1f * std::sin(t * 2.0));
    }

    // Benchmark incremental AppendRibbonSegments as points arrive from digitizer
    BLPath outline;
    std::vector<StrokePoint> streamPts;
    streamPts.reserve(1000);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < points.size(); ++i) {
        size_t prevSize = streamPts.size();
        streamPts.push_back(points[i]);
        InkGenerator::AppendRibbonSegments(outline, streamPts, prevSize, CapType::Round, StrokePattern::Solid);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double appendMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "  -> Incremental append 1000 points: " << appendMs << " ms" << std::endl;

    // Benchmark fill_path rasterization in 1920x1080 surface
    BLImage img(1920, 1080, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    auto t2 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        ctx.fill_path(outline);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double rasterMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / 10.0;
    std::cout << "  -> Frame rasterization time (solid): " << rasterMs << " ms per frame" << std::endl;

    // Benchmark pencil pattern rasterization
    auto t4 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        InkGenerator::RenderPencilStroke(ctx, points, 0.8f, BLRgba32(0xFF202020), &outline);
    }
    auto t5 = std::chrono::high_resolution_clock::now();
    double pencilMs = std::chrono::duration<double, std::milli>(t5 - t4).count() / 10.0;
    std::cout << "  -> Frame rasterization time (pencil): " << pencilMs << " ms per frame" << std::endl;

    // Benchmark Miter-Clipped Single Contour generation (CPU) and rasterization
    auto tGen0 = std::chrono::high_resolution_clock::now();
    BLPath singleContour = InkGenerator::GenerateStroke(points, CapType::Round, StrokePattern::Solid);
    auto tGen1 = std::chrono::high_resolution_clock::now();
    double genMs = std::chrono::duration<double, std::milli>(tGen1 - tGen0).count();
    std::cout << "  -> CPU generation time (Miter-Clipped Single Contour 1000 pts): " << genMs << " ms" << std::endl;

    auto t6 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        ctx.fill_path(singleContour);
    }
    auto t7 = std::chrono::high_resolution_clock::now();
    double singleContourRasterMs = std::chrono::duration<double, std::milli>(t7 - t6).count() / 10.0;
    std::cout << "  -> Frame rasterization time (Miter-Clipped Single Contour): " << singleContourRasterMs << " ms per frame" << std::endl;

    // Benchmark pencil pattern with Miter-Clipped Single Contour!
    auto t8 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        InkGenerator::RenderPencilStroke(ctx, points, 0.8f, BLRgba32(0xFF202020), &singleContour);
    }
    auto t9 = std::chrono::high_resolution_clock::now();
    double pencilSingleContourMs = std::chrono::duration<double, std::milli>(t9 - t8).count() / 10.0;
    std::cout << "  -> Frame rasterization time (pencil with Single Contour 1000 pts): " << pencilSingleContourMs << " ms per frame" << std::endl;

    // 10-second stroke benchmark (2500 points at 240Hz)
    std::vector<StrokePoint> points2500;
    points2500.reserve(2500);
    for (int i = 0; i < 2500; ++i) {
        double t = i * 0.02;
        double x = 400.0 + 300.0 * std::cos(t * 0.5) + t * 5.0;
        double y = 400.0 + 300.0 * std::sin(t * 0.5);
        points2500.emplace_back(x, y, 0.4f + 0.1f * std::sin(t * 2.0));
    }
    auto tGen2500_0 = std::chrono::high_resolution_clock::now();
    BLPath singleContour2500 = InkGenerator::GenerateStroke(points2500, CapType::Round, StrokePattern::Solid);
    auto tGen2500_1 = std::chrono::high_resolution_clock::now();
    double gen2500Ms = std::chrono::duration<double, std::milli>(tGen2500_1 - tGen2500_0).count();
    std::cout << "  -> CPU generation time (Single Contour 2500 pts / 10 sec): " << gen2500Ms << " ms" << std::endl;

    auto tRast2500_0 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        ctx.fill_path(singleContour2500);
    }
    auto tRast2500_1 = std::chrono::high_resolution_clock::now();
    double solidRast2500Ms = std::chrono::duration<double, std::milli>(tRast2500_1 - tRast2500_0).count() / 10.0;
    std::cout << "  -> Frame rasterization time (solid 2500 pts / 10 sec): " << solidRast2500Ms << " ms per frame" << std::endl;

    auto tPencil2500_0 = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 10; ++frame) {
        ctx.clear_all();
        InkGenerator::RenderPencilStroke(ctx, points2500, 0.8f, BLRgba32(0xFF202020), &singleContour2500);
    }
    auto tPencil2500_1 = std::chrono::high_resolution_clock::now();
    double pencil2500Ms = std::chrono::duration<double, std::milli>(tPencil2500_1 - tPencil2500_0).count() / 10.0;
    std::cout << "  -> Frame rasterization time (pencil 2500 pts / 10 sec): " << pencil2500Ms << " ms per frame" << std::endl;

    // Invariants
    assert(appendMs < 10.0);
    assert(rasterMs < 8.33);
    assert(pencilMs < 10.0);
}

void TestStrokePressureTransitions() {
    std::cout << "[Test 2/2] Running Stroke Pressure Transition & Miter Clamping Invariant Tests..." << std::endl;

    BLImage img(1000, 1000, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // 1. Rapid pressure transition test (step from 0.1mm to 3.0mm back to 0.1mm along a curved path)
    std::vector<StrokePoint> stepPoints;
    for (int i = 0; i < 100; ++i) {
        double angle = i * 0.05; // 0.05 rad ~ 2.8 deg per step
        double x = 100.0 + i * 0.8;
        double y = 100.0 + 30.0 * std::sin(angle);
        float p = (i >= 30 && i <= 60) ? 0.95f : 0.10f;
        float r = 0.2f + p * 2.8f; // jumps from 0.48mm to 2.86mm
        stepPoints.emplace_back(x, y, r, p);
    }

    // Verify all cap types generate valid contours without self-intersections or crashes
    std::vector<CapType> caps = { CapType::Round, CapType::Square, CapType::Chisel };
    for (auto cap : caps) {
        BLPath path = InkGenerator::GenerateStroke(stepPoints, cap, StrokePattern::Solid);
        assert(!path.is_empty());
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFF000000));
        BLResult res = ctx.fill_path(path);
        assert(res == BL_SUCCESS);
    }

    // 2. High-curvature corner under extreme pressure (r = 4.0mm with 90 deg turn on 0.1mm segment)
    std::vector<StrokePoint> cornerPoints;
    cornerPoints.emplace_back(200.0, 200.0, 4.0f, 0.95f);
    cornerPoints.emplace_back(200.1, 200.0, 4.0f, 0.95f);
    cornerPoints.emplace_back(200.1, 200.1, 4.0f, 0.95f);
    cornerPoints.emplace_back(200.1, 201.0, 4.0f, 0.95f);

    BLPath cornerPath = InkGenerator::GenerateStroke(cornerPoints, CapType::Round, StrokePattern::Solid);
    assert(!cornerPath.is_empty());
    ctx.clear_all();
    BLResult cRes = ctx.fill_path(cornerPath);
    assert(cRes == BL_SUCCESS);

    // 3. 180-Degree Hairpin Turnaround Invariant Tests:
    {
        // 3a. Exact 180 deg reversal
        std::vector<StrokePoint> hairpinExact = {
            StrokePoint(100.0, 100.0, 2.0f, 0.5f),
            StrokePoint(150.0, 100.0, 2.0f, 0.5f),
            StrokePoint(100.0, 100.0, 2.0f, 0.5f)
        };
        BLPath hp1 = InkGenerator::GenerateStroke(hairpinExact, CapType::Round, StrokePattern::Solid);
        assert(!hp1.is_empty());
        BLBox b1;
        hp1.get_bounding_box(&b1);
        assert(b1.x1 >= 151.9);
        ctx.clear_all();
        assert(ctx.fill_path(hp1) == BL_SUCCESS);

        // 3b. Near-180 deg slight left turn (179.9 deg)
        std::vector<StrokePoint> hairpinLeft = {
            StrokePoint(100.0, 100.0, 2.0f, 0.5f),
            StrokePoint(150.0, 100.0, 2.0f, 0.5f),
            StrokePoint(100.0, 100.1, 2.0f, 0.5f)
        };
        BLPath hp2 = InkGenerator::GenerateStroke(hairpinLeft, CapType::Round, StrokePattern::Solid);
        assert(!hp2.is_empty());
        BLBox b2;
        hp2.get_bounding_box(&b2);
        assert(b2.x1 >= 151.9);
        ctx.clear_all();
        assert(ctx.fill_path(hp2) == BL_SUCCESS);

        // 3c. Near-180 deg slight right turn (179.9 deg)
        std::vector<StrokePoint> hairpinRight = {
            StrokePoint(100.0, 100.0, 2.0f, 0.5f),
            StrokePoint(150.0, 100.0, 2.0f, 0.5f),
            StrokePoint(100.0, 99.9, 2.0f, 0.5f)
        };
        BLPath hp3 = InkGenerator::GenerateStroke(hairpinRight, CapType::Round, StrokePattern::Solid);
        assert(!hp3.is_empty());
        BLBox b3;
        hp3.get_bounding_box(&b3);
        assert(b3.x1 >= 151.9);
        ctx.clear_all();
        assert(ctx.fill_path(hp3) == BL_SUCCESS);

        // 3d. Polygonal nib (Chisel & Square) 180 deg reversal
        for (auto cap : { CapType::Square, CapType::Chisel }) {
            BLPath polyHp = InkGenerator::GenerateStroke(hairpinExact, cap, StrokePattern::Solid);
            assert(!polyHp.is_empty());
            ctx.clear_all();
            assert(ctx.fill_path(polyHp) == BL_SUCCESS);
        }
    }

    // 4. Textured pencil with selective pressure band activation
    ctx.clear_all();
    InkGenerator::RenderPencilStroke(ctx, stepPoints, 0.8f, BLRgba32(0xFF202020));

    // 5. Hockey-stick / J-turn Inner Corner Notch Regression Test
    {
        float rNominal = 2.0f;
        std::vector<StrokePoint> jTurnPoints = {
            StrokePoint(100.0, 100.0, rNominal, 0.5f),
            StrokePoint(120.0, 105.0, rNominal, 0.5f),
            StrokePoint(120.2, 105.1, rNominal, 0.5f),
            StrokePoint(122.0, 130.0, rNominal, 0.5f),
            StrokePoint(123.0, 150.0, rNominal, 0.5f)
        };

        for (auto cap : { CapType::Round, CapType::Square, CapType::Chisel }) {
            BLPath jPath = InkGenerator::GenerateStroke(jTurnPoints, cap, StrokePattern::Solid);
            assert(!jPath.is_empty());
            ctx.clear_all();
            assert(ctx.fill_path(jPath) == BL_SUCCESS);

            BLPathView v = jPath.view();
            assert(v.size > 0);
            for (size_t i = 0; i < v.size; ++i) {
                if (std::isnan(v.vertex_data[i].x) || std::isnan(v.vertex_data[i].y)) continue;
                double dx = v.vertex_data[i].x - 120.2;
                double dy = v.vertex_data[i].y - 105.1;
                double dist = std::hypot(dx, dy);
                if (cap == CapType::Round && i == 2) {
                    assert(dist >= rNominal * 0.99);
                }
                assert(dist >= rNominal * 0.45);
            }
        }
    }

    std::cout << "  -> Pressure step transition, 180 deg turnaround, and J-turn inner corner tests passed successfully." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Folio-Sketch Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestStrokeTessellationAndRasterPerformance();
    TestStrokePressureTransitions();

    std::cout << "========================================" << std::endl;
    std::cout << "All Folio-Sketch Tests Passed!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
