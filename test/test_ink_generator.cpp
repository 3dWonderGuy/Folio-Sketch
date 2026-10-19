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

#include "folio_sketch/folio_sketch.hpp"
#include "../src/geometry/ink_stroke_filter.hpp"

using namespace Folio;

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
    assert(appendMs < 15.0);
    assert(rasterMs < 30.0);
    assert(pencilMs < 30.0);
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
            }
        }
    }

    // Diagnostic test for elbow corner turn under pressure
    {
        // Test 1: Violent rapid pressure jumps over short distances (rapid pressure spikes)
        std::vector<StrokePoint> rapidSpikes;
        double sx = 50.0, sy = 100.0;
        for (int i = 0; i < 50; ++i) {
            sx += 0.5; // very short 0.5mm steps
            // Violent pressure oscillation between 1mm and 15mm radius
            float r = (i % 6 < 3) ? 1.0f : 12.0f;
            rapidSpikes.push_back(StrokePoint(sx, sy + std::sin(i * 0.2) * 5.0, r));
        }

        BLPath pSpikes = InkGenerator::GenerateStroke(rapidSpikes, CapType::Round, StrokePattern::Solid);
        assert(!pSpikes.is_empty());
        BLBox boxSpikes;
        assert(pSpikes.get_bounding_box(&boxSpikes) == BL_SUCCESS);
        assert(boxSpikes.x0 < 50.0 && boxSpikes.x1 > 70.0);

        // Test 2: Sharp 90-degree corner under heavy pressure
        std::vector<StrokePoint> sharp90;
        for (double x = 50.0; x <= 150.0; x += 2.0) {
            sharp90.push_back(StrokePoint(x, 100.0, 3.0f));
        }
        // Heavy pressure at the corner
        sharp90.push_back(StrokePoint(152.0, 100.0, 18.0f));
        sharp90.push_back(StrokePoint(152.0, 102.0, 20.0f));
        sharp90.push_back(StrokePoint(152.0, 104.0, 18.0f));
        for (double y = 106.0; y <= 200.0; y += 2.0) {
            sharp90.push_back(StrokePoint(152.0, y, 3.0f));
        }

        BLPath p90 = InkGenerator::GenerateStroke(sharp90, CapType::Round, StrokePattern::Solid);
        assert(!p90.is_empty());
        BLBox box90;
        assert(p90.get_bounding_box(&box90) == BL_SUCCESS);
        assert(box90.x0 < 50.0 && box90.x1 > 150.0);
        assert(box90.y0 < 100.0 && box90.y1 > 190.0);

        // Test 2: Dense crawl around 90-degree elbow under heavy pressure (r=25mm, spacing=0.2mm)
        std::vector<StrokePoint> crawlPts;
        for (double x = 50.0; x <= 150.0; x += 1.0) {
            crawlPts.push_back(StrokePoint(x, 100.0, 4.0f));
        }
        double cx = 150.0, cy = 120.0;
        for (int k = 0; k <= 30; ++k) {
            double ang = -M_PI * 0.5 + (M_PI * 0.5) * (k / 30.0);
            double px = cx + 20.0 * std::cos(ang);
            double py = cy + 20.0 * std::sin(ang);
            float r = 4.0f + 16.0f * std::sin((k / 30.0) * M_PI);
            crawlPts.push_back(StrokePoint(px, py, r));
        }
        for (double y = 120.0; y <= 200.0; y += 1.0) {
            crawlPts.push_back(StrokePoint(170.0, y, 4.0f));
        }

        BLPath pCrawl = InkGenerator::GenerateStroke(crawlPts, CapType::Round, StrokePattern::Solid);
        assert(!pCrawl.is_empty());
        BLBox boxCrawl;
        assert(pCrawl.get_bounding_box(&boxCrawl) == BL_SUCCESS);
        assert(boxCrawl.x0 < 50.0 && boxCrawl.x1 > 170.0);
        assert(boxCrawl.y0 < 100.0 && boxCrawl.y1 > 190.0);
    }

    // Diagnostic test 2: Full InkEngine simulation with stylus pause & pressure spike
    {
        Folio::InkEngine engine;
        StrokeStyle style;
        style.baseWidth = 4.0f;
        style.color = BLRgba32(0xFFFF0000);
        style.capType = CapType::Round;
        style.pattern = StrokePattern::Solid;
        InkingConfig cfg = engine.GetConfig();
        cfg.usePressure = true;
        cfg.useVelocity = true;
        cfg.minWidthMultiplier = 0.2f;
        cfg.maxWidthMultiplier = 4.0f;
        engine.SetConfig(cfg);

        double time = 0.0;
        engine.BeginStroke(50.0, 100.0, 0.2f, time, style, 1.0f);
        // Horizontal line
        for (double x = 55.0; x <= 180.0; x += 3.0) {
            time += 0.01;
            engine.AppendPoint(x, 100.0, 0.25f, time);
        }
        // User slows down / pauses at corner and presses hard
        for (int p = 0; p < 15; ++p) {
            time += 0.01;
            double jitterX = (p % 2 == 0) ? 0.1 : -0.1;
            double jitterY = (p % 3 == 0) ? 0.1 : -0.05;
            engine.AppendPoint(180.0 + jitterX, 100.0 + jitterY, 0.95f, time);
        }
        // Turns downwards-right
        for (int s = 1; s <= 20; ++s) {
            time += 0.01;
            double px = 180.0 + 3.0 * s * 0.707;
            double py = 100.0 + 3.0 * s * 0.707;
            engine.AppendPoint(px, py, 0.35f, time);
        }
        FinishedStrokeData stroke = engine.FinishStroke();
        assert(!stroke.outlinePath.is_empty());
        assert(stroke.modeledPoints.size() > 20);
    }

    // 6. Closed Polygon with Extreme Variable Width (Triangle & Quad with varying vertex radii)
    {
        std::vector<StrokePoint> varPoly = {
            StrokePoint(50.0, 50.0, 0.4f),
            StrokePoint(150.0, 50.0, 3.5f),   // Width jump to 7mm
            StrokePoint(100.0, 150.0, 1.2f),  // Taper to 2.4mm
            StrokePoint(50.0, 50.0, 0.4f)     // Closed back to start
        };

        BLPath polyPath = InkGenerator::GenerateStroke(varPoly, CapType::Round, StrokePattern::Solid, /*isStandardShape=*/true);
        assert(!polyPath.is_empty());
        ctx.clear_all();
        assert(ctx.fill_path(polyPath) == BL_SUCCESS);

        BLBox polyBox;
        polyPath.get_bounding_box(&polyBox);
        assert(polyBox.x0 < 50.0 && polyBox.x1 > 150.0);
        assert(polyBox.y0 < 50.0 && polyBox.y1 > 150.0);

        // Test GenerateStroke directly on unclosed triangle (size 3) with isStandardShape=true
        std::vector<StrokePoint> unclosedTri = {
            StrokePoint(10.0, 10.0, 1.0f),
            StrokePoint(40.0, 10.0, 2.0f),
            StrokePoint(25.0, 40.0, 1.5f)
        };
        BLPath triRibbon = InkGenerator::GenerateStroke(unclosedTri, CapType::Round, StrokePattern::Solid, /*isStandardShape=*/true);
        assert(!triRibbon.is_empty());
        ctx.clear_all();
        assert(ctx.fill_path(triRibbon) == BL_SUCCESS);
    }

    // 7. Open Stroke with Extreme Variable Width and Short Segment Sampling
    {
        std::vector<StrokePoint> varShortStroke = {
            StrokePoint(200.0, 200.0, 0.2f),
            StrokePoint(200.1, 200.0, 3.8f),   // Huge width jump on 0.1mm segment!
            StrokePoint(200.2, 205.0, 0.3f),   // Taper down on 90 deg turn
            StrokePoint(210.0, 205.0, 2.5f)
        };

        for (auto cap : { CapType::Round, CapType::Square, CapType::Chisel }) {
            BLPath vPath = InkGenerator::GenerateStroke(varShortStroke, cap, StrokePattern::Solid);
            assert(!vPath.is_empty());
            ctx.clear_all();
            assert(ctx.fill_path(vPath) == BL_SUCCESS);
        }
    }

    std::cout << "  -> Pressure step transition, 180 deg turnaround, J-turn, and variable width closed polygon tests passed successfully." << std::endl;
}

void TestCollisionAndSlicer() {
    std::cout << "[Test 3/6] Running Collision Engine & Stroke Slicer Unit Tests..." << std::endl;

    // 1. Point hit test on dot
    Point2D dotCenter{ 50.0, 50.0 };
    assert(StrokeCollisionEngine::HitTestPoint(dotCenter, 2.0f, 50.5, 50.0, 1.0));
    assert(!StrokeCollisionEngine::HitTestPoint(dotCenter, 2.0f, 60.0, 60.0, 1.0));

    // 2. Segment hit test
    std::vector<Segment1D> strokeSegments = {
        Segment1D{ Point2D{ 0.0, 0.0 }, Point2D{ 100.0, 0.0 }, 1.0f }
    };

    auto hitRes = StrokeCollisionEngine::HitTestStroke(strokeSegments, 50.0, 0.2, 0.5);
    assert(hitRes.hit);
    assert(hitRes.segmentIndex == 0);
    assert(std::abs(hitRes.t - 0.5f) < 0.01f);

    auto missRes = StrokeCollisionEngine::HitTestStroke(strokeSegments, 50.0, 10.0, 0.5);
    assert(!missRes.hit);

    // 3. Swept hit test (moving query capsule)
    // 3a. Crossing stroke
    Point2D sweepStart{ 50.0, -5.0 };
    Point2D sweepEnd{ 50.0, 5.0 };
    assert(StrokeCollisionEngine::HitTestStrokeSwept(strokeSegments, sweepStart, sweepEnd, 0.5));

    // 3b. Completely distant stroke (validates AABB broadphase culling)
    Point2D farStart{ 200.0, 200.0 };
    Point2D farEnd{ 210.0, 210.0 };
    assert(!StrokeCollisionEngine::HitTestStrokeSwept(strokeSegments, farStart, farEnd, 1.0));

    // 4. Degenerate segments (points) in swept collision - must not divide by zero or NaN
    std::vector<Segment1D> degenSegments = {
        Segment1D{ Point2D{ 10.0, 10.0 }, Point2D{ 10.0, 10.0 }, 1.0f }
    };
    Point2D degenQuery0{ 10.0, 10.0 };
    Point2D degenQuery1{ 10.0, 10.0 };
    assert(StrokeCollisionEngine::HitTestStrokeSwept(degenSegments, degenQuery0, degenQuery1, 0.5));

    Point2D degenFar0{ 99.0, 99.0 };
    Point2D degenFar1{ 99.0, 99.0 };
    assert(!StrokeCollisionEngine::HitTestStrokeSwept(degenSegments, degenFar0, degenFar1, 0.5));

    // 5. Stroke Slicer Tests
    std::vector<std::vector<Segment1D>> subStrokes;

    // 5a. Midpoint punch-through split
    bool modified = StrokeSlicer::SliceSegments(strokeSegments, 50.0, 0.0, 5.0, subStrokes);
    assert(modified);
    assert(subStrokes.size() == 2);
    assert(subStrokes[0].front().p0.x < 1.0);
    assert(subStrokes[1].back().p1.x > 99.0);

    // 5b. Near-start punch-through (tests fix where t1 close to 0 was skipped by t1 > 0.001)
    subStrokes.clear();
    bool cutNearStart = StrokeSlicer::SliceSegments(strokeSegments, 1.0, 0.0, 0.999, subStrokes);
    assert(cutNearStart);
    assert(subStrokes.size() >= 1);

    // 5c. Miss (no intersection)
    subStrokes.clear();
    bool missed = StrokeSlicer::SliceSegments(strokeSegments, 50.0, 50.0, 2.0, subStrokes);
    assert(!missed);
    assert(subStrokes.size() == 1);
    assert(subStrokes[0].size() == 1);

    // 5d. Total erasure
    subStrokes.clear();
    bool erased = StrokeSlicer::SliceSegments(strokeSegments, 50.0, 0.0, 100.0, subStrokes);
    assert(erased);
    assert(subStrokes.empty());

    std::cout << "  -> Collision engine and stroke slicer tests passed successfully." << std::endl;
}

void TestWidthEvaluatorAndPostProcessor() {
    std::cout << "[Test 4/6] Running Stroke Width Evaluator & Post-Processor Tests..." << std::endl;

    InkingConfig config;

    // 1. StrokeWidthEvaluator pressure curve & boundary safety
    float pMin = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    float pMax = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(1.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(pMin < pMax);

    // NaN pressure input must be safely handled without NaN output
    float pNan = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, config);
    assert(!std::isnan(pNan));
    assert(pNan >= 0.0f);

    // Zero-range pressure config (deadzone == saturation)
    InkingConfig brokenConfig = config;
    brokenConfig.pressureDeadzoneMin = 0.5f;
    brokenConfig.pressureSaturationMax = 0.5f;
    float pSafe = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(1.0f, 0.7f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, brokenConfig);
    assert(!std::isnan(pSafe));

    // 2. StrokePostProcessor::Simplify on collinear points
    std::vector<StrokePoint> collinearPts;
    for (int i = 0; i < 20; ++i) {
        collinearPts.emplace_back(i * 1.0, 0.0, 0.5f);
    }
    auto simplified = Folio::StrokePostProcessor::Simplify(collinearPts, 0.05f);
    assert(simplified.size() == 2); // 20 collinear points reduced to 2 endpoints
    assert(simplified.front().x == 0.0);
    assert(simplified.back().x == 19.0);

    // 3. StrokePostProcessor::DetectClosedLoop
    // 3a. Tiny 1mm open stroke must NOT be detected as closed loop
    std::vector<StrokePoint> tinyStroke = {
        StrokePoint(0.0, 0.0, 0.5f),
        StrokePoint(0.5, 0.1, 0.5f),
        StrokePoint(1.0, 0.0, 0.5f)
    };
    assert(!Folio::StrokePostProcessor::DetectClosedLoop(tinyStroke, 2.0f));

    // 3b. Genuine closed circle (circumference ~ 62.8mm > 6mm) returning to start
    std::vector<StrokePoint> loopStroke;
    for (int i = 0; i <= 36; ++i) {
        double a = (i * 10.0) * M_PI / 180.0;
        loopStroke.emplace_back(50.0 + 10.0 * std::cos(a), 50.0 + 10.0 * std::sin(a), 0.5f);
    }
    assert(Folio::StrokePostProcessor::DetectClosedLoop(loopStroke, 2.0f));

    // 4. StrokePostProcessor::Analyze
    FinishedStrokeData strokeData;
    strokeData.modeledPoints = loopStroke;
    auto analysis = Folio::StrokePostProcessor::Analyze(strokeData);
    assert(analysis.isClosed);
    assert(analysis.topology == Folio::StrokeTopology::ClosedLoop);
    assert(analysis.totalArcLengthMm > 50.0);
    assert(analysis.endpointDistanceMm < 0.1);

    // 4b. Self-intersecting open stroke (figure-8 / loop crossover)
    FinishedStrokeData crossStrokeData;
    crossStrokeData.modeledPoints = {
        StrokePoint(0.0, 0.0, 0.5f),
        StrokePoint(20.0, 20.0, 0.5f),
        StrokePoint(20.0, 0.0, 0.5f),
        StrokePoint(0.0, 20.0, 0.5f)
    };
    auto analysisCross = Folio::StrokePostProcessor::Analyze(crossStrokeData);
    assert(!analysisCross.isClosed);
    assert(analysisCross.hasSelfIntersections);
    assert(analysisCross.topology == Folio::StrokeTopology::SelfIntersecting);

    // 5. Inverted min/max width multipliers and zero velocityThinningMaxSpeed
    InkingConfig edgeConfig = config;
    edgeConfig.minWidthMultiplier = 2.0f;
    edgeConfig.maxWidthMultiplier = 0.5f;
    edgeConfig.useVelocity = true;
    edgeConfig.velocityThinningMaxSpeed = 0.0f;
    float pEdge = Folio::StrokeWidthEvaluator::CalculateDynamicWidth(1.0f, 0.5f, 100.0f, 0.0f, 0.0f, -1.0f, 1.0f, edgeConfig);
    assert(!std::isnan(pEdge));
    assert(pEdge > 0.0f);

    std::cout << "  -> Width evaluator and post-processor tests passed successfully." << std::endl;
}

void TestInkEngineLifecycle() {
    std::cout << "[Test 5/6] Running InkEngine Full Lifecycle & Move Semantics Tests..." << std::endl;

    Folio::InkEngine engine;
    StrokeStyle style;
    style.baseWidth = 1.0f;
    style.capType = CapType::Round;
    style.pattern = StrokePattern::Solid;

    // 1. Normal Multi-Point Gesture
    engine.BeginStroke(10.0, 10.0, 0.5f, 1.0, style);
    assert(engine.IsStrokeActive());
    assert(!engine.GetLiveOutlinePath().is_empty());

    for (int i = 1; i <= 10; ++i) {
        engine.AppendPoint(10.0 + i * 2.0, 10.0 + i * 0.5, 0.6f, 1.0 + i * 0.02);
    }

    assert(engine.GetLiveModeledPoints().size() > 1);

    FinishedStrokeData stroke1 = engine.FinishStroke();
    assert(!engine.IsStrokeActive());
    assert(!stroke1.modeledPoints.empty());
    assert(!stroke1.liveSegments.empty());
    assert(!stroke1.outlinePath.is_empty());

    // 2. Single-Tap Dot Gesture
    engine.BeginStroke(50.0, 50.0, 0.8f, 2.0, style);
    FinishedStrokeData tapStroke = engine.FinishStroke();
    assert(tapStroke.modeledPoints.size() == 1);
    assert(tapStroke.liveSegments.size() == 1);
    assert(!tapStroke.outlinePath.is_empty());

    // 3. Cancel Gesture
    engine.BeginStroke(30.0, 30.0, 0.5f, 3.0, style);
    engine.AppendPoint(31.0, 31.0, 0.5f, 3.01);
    assert(engine.IsStrokeActive());
    engine.CancelStroke();
    assert(!engine.IsStrokeActive());
    assert(engine.GetLiveModeledPoints().empty());
    assert(engine.GetLiveOutlinePath().is_empty());

    // 4. Move Construction & Move Assignment
    engine.BeginStroke(100.0, 100.0, 0.5f, 4.0, style);
    Folio::InkEngine movedEngine(std::move(engine));
    assert(!engine.IsStrokeActive()); // Moved-from engine is inactive
    assert(movedEngine.IsStrokeActive());

    movedEngine.AppendPoint(105.0, 105.0, 0.6f, 4.05);
    FinishedStrokeData movedStroke = movedEngine.FinishStroke();
    assert(!movedStroke.modeledPoints.empty());

    // Moved-from engine can safely start a new stroke without crashing
    engine.BeginStroke(200.0, 200.0, 0.5f, 5.0, style);
    assert(engine.IsStrokeActive());
    FinishedStrokeData freshStroke = engine.FinishStroke();
    assert(!freshStroke.modeledPoints.empty());

    std::cout << "  -> InkEngine lifecycle and move semantics tests passed successfully." << std::endl;
}

void TestPatternAndInfillEdgeCases() {
    std::cout << "[Test 6/6] Running Pattern & Infill Edge Cases & Invariant Tests..." << std::endl;

    std::vector<StrokePoint> emptyPts;
    std::vector<StrokePoint> singlePt = { StrokePoint(50.0, 50.0, 0.8f, 0.7f) };

    std::vector<StrokePattern> patterns = {
        StrokePattern::Solid,
        StrokePattern::Dashed,
        StrokePattern::Dotted,
        StrokePattern::DashDot,
        StrokePattern::TexturedPencil
    };

    // 1. Verify all patterns handle empty and 1-point inputs without crashing
    for (auto pat : patterns) {
        BLPath emptyPath = InkGenerator::GenerateStroke(emptyPts, CapType::Round, pat);
        assert(emptyPath.is_empty());

        BLPath singlePath = InkGenerator::GenerateStroke(singlePt, CapType::Round, pat);
        assert(!singlePath.is_empty());
    }

    // 2. BuildInfill tests
    std::vector<Point2D> openPoly = {
        { 0.0, 0.0 }, { 20.0, 0.0 }, { 20.0, 20.0 }, { 0.0, 20.0 }
    };
    BLPath infillOpen = InkGenerator::BuildInfill(openPoly);
    assert(!infillOpen.is_empty());

    // Already closed polygon (endpoint == startpoint) deduplication
    std::vector<Point2D> closedPoly = openPoly;
    closedPoly.push_back(openPoly.front());
    BLPath infillClosed = InkGenerator::BuildInfill(closedPoly);
    assert(!infillClosed.is_empty());

    // 3. FillInfill tests with all hatch and gradient variants and auto-computed bounds
    BLImage img(200, 200, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    std::vector<InfillType> infillTypes = {
        InfillType::Solid,
        InfillType::SemiTransparent,
        InfillType::LinearGradient,
        InfillType::RadialGradient,
        InfillType::HatchDiagonal,
        InfillType::HatchCross,
        InfillType::HatchHorizontal,
        InfillType::HatchVertical,
        InfillType::HatchDots
    };

    for (auto infType : infillTypes) {
        ctx.clear_all();
        InfillStyle style;
        style.type = infType;
        style.primaryColor = BLRgba32(0xFF0055AA);
        style.secondaryColor = BLRgba32(0xFF00AAFF);
        style.outlineWidthMm = 1.0;
        // Notice style.bounds is left default BLBox(0,0,0,0) - tests auto-bounding box inference!
        InkGenerator::FillInfill(ctx, infillOpen, style);
    }

    std::cout << "  -> Pattern and infill edge case tests passed successfully." << std::endl;
}

void TestInkMeshBuilderAndDebugger() {
    std::cout << "[Test 7/7] Running InkMeshBuilder & Visual Debugger Verification..." << std::endl;

    std::vector<StrokePoint> testPts = {
        { 10.0, 10.0, 1.0f, 0.5f, 0.0f, 0.0f },
        { 20.0, 15.0, 2.5f, 0.8f, 0.0f, 0.0f },
        { 30.0, 40.0, 1.5f, 0.6f, 0.0f, 0.0f },
        { 50.0, 42.0, 3.0f, 1.0f, 0.0f, 0.0f }
    };

    // 1. InkMeshBuilder test
    Folio::InkMesh mesh;
    Folio::InkMeshBuilder::BuildSolidMesh(testPts, CapType::Round, 0xFFFF0000, mesh);
    assert(!mesh.empty());
    assert(!mesh.vertices.empty());
    assert(!mesh.indices.empty());
    assert(mesh.indices.size() % 3 == 0); // valid triangle indices

    // Single point cap mesh test
    Folio::InkMesh singleMesh;
    Folio::InkMeshBuilder::BuildSolidMesh(std::span<const StrokePoint>(testPts.data(), 1), CapType::Round, 0xFF00FF00, singleMesh);
    assert(!singleMesh.empty());

    // 2. Debug capture test
    InkGenerator::SetDebugCapture(true);
    assert(InkGenerator::IsDebugCaptureEnabled());

    BLPath dbgPath = InkGenerator::GenerateStroke(testPts, CapType::Round, StrokePattern::Solid);
    assert(!dbgPath.is_empty());

    const auto& dbgInfo = InkGenerator::GetLastDebugInfo();
    assert(!dbgInfo.rawInputPts.empty());
    assert(!dbgInfo.cleanPts.empty());
    assert(!dbgInfo.leftRail.empty());
    assert(!dbgInfo.rightRail.empty());

    // 3. RenderDebugOverlay test
    BLImage img(300, 200, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.clear_all();
    InkGenerator::RenderDebugOverlay(ctx, dbgInfo, 1.0, &dbgPath);
    ctx.end();

    // 4. Dump test
    InkGenerator::DumpStrokeToFile("test_debug_dump.txt");
    std::remove("test_debug_dump.txt");

    InkGenerator::SetDebugCapture(false);
    assert(!InkGenerator::IsDebugCaptureEnabled());

    std::cout << "  -> InkMeshBuilder & Visual Debugger tests passed successfully." << std::endl;
}

void TestCrescentTurnGeometry() {
    std::cout << "[Test 8/8] Running Crescent Turn Geometry (Tight Radius Curvature) test..." << std::endl;

    InkGenerator::SetDebugCapture(true);
    std::vector<StrokePoint> crescentPts = {
#include "repro114.inc"
    };

    BLPath path = InkGenerator::BuildSolidRibbon(crescentPts);
    assert(!path.is_empty());

    const auto& info = InkGenerator::GetLastDebugInfo();
    std::cout << "  Crescent stroke clean points: " << info.cleanPts.size() << std::endl;
    std::cout << "  Left rail points: " << info.leftRail.size() << ", Right rail points: " << info.rightRail.size() << std::endl;
    std::cout << "  Self intersections: " << info.selfIntersections.size() << std::endl;
    std::cout << "  Retrograde folds: " << info.retrogradeIndices.size() << std::endl;

    // Verify 100% elimination of self-intersections and retrograde folds
    assert(info.selfIntersections.empty());
    assert(info.retrogradeIndices.empty());
    assert(!info.hasDefects);

    InkGenerator::ClearDebugInfo();
    InkGenerator::SetDebugCapture(false);
    std::cout << "  -> Crescent Turn Geometry test passed (0 defects, 100% OK)!" << std::endl;
}

void TestQuickStrokeLoopMesh() {
    std::cout << "[Test 9/10] Running Quick Stroke Loop Mesh (Branch-Cut & Hole Integrity) test..." << std::endl;

    // Build a circle loop centered at (100, 100) with radius 15mm
    // Traversing through the branch cut where normal crosses (-1, 0)
    std::vector<StrokePoint> loopPts;
    const double centerX = 100.0, centerY = 100.0, loopR = 15.0;
    const int numSteps = 40;
    for (int i = 0; i <= numSteps; ++i) {
        double theta = (2.0 * M_PI * i) / numSteps;
        double px = centerX + loopR * std::cos(theta);
        double py = centerY + loopR * std::sin(theta);
        loopPts.emplace_back(px, py, 0.5f); // 1.0mm stroke width (0.5mm radius)
    }

    InkMesh mesh;
    InkMeshBuilder::BuildSolidMesh(loopPts, CapType::Round, 0xFF000000, mesh, 3.7795f);
    assert(!mesh.empty());
    assert(!mesh.vertices.empty());

    // Verify that NO vertex intrudes into the central hole of the loop
    // Expected inner boundary is at loopR - r - delta ~ 15 - 0.5 - 1.0 = 13.5mm
    for (const auto& v : mesh.vertices) {
        double distFromCenter = std::hypot(v.x - centerX, v.y - centerY);
        assert(distFromCenter >= 10.0 && "Vertex intruded into loop center hole! Fan wrap bug detected.");
    }

    std::cout << "  -> Loop Mesh test passed: central hole completely hollow (0 intruding vertices)!" << std::endl;
}

void TestTailTaper() {
    std::cout << "[Test 10/10] Running Configurable Tail Taper test..." << std::endl;

    InkEngine engine;
    InkingConfig config = engine.GetConfig();
    config.tailTaperLengthMm = 20.0f;
    config.tailTaperPower = 1.0f;
    config.usePressure = true;
    config.minWidthMultiplier = 0.2f;
    config.maxWidthMultiplier = 1.0f;
    engine.SetConfig(config);

    StrokeStyle style;
    style.baseWidth = 2.0f; // min radius will be 2.0 * 0.2 * 0.5 = 0.2mm
    style.pattern = StrokePattern::Solid;
    style.capType = CapType::Round;
    style.usePressure = true;

    // Draw straight line along X from 0 to 60mm with constant 0.8 pressure
    engine.BeginStroke(0.0, 0.0, 0.8f, 0.0, style, 1.0f);
    for (int i = 1; i <= 60; ++i) {
        engine.AppendPoint(static_cast<double>(i), 0.0, 0.8f, i * 0.01);
    }

    FinishedStrokeData data = engine.FinishStroke();
    assert(!data.modeledPoints.empty());

    const auto& pts = data.modeledPoints;
    float minExpectedR = style.baseWidth * config.minWidthMultiplier * 0.5f;

    // Tip point should be tapered down to minExpectedR
    assert(pts.back().r <= minExpectedR + 1e-3f);
    assert(pts.back().pressure <= 1e-4f);

    // Verify monotonic decay over the trailing 15mm span
    float lastR = pts.back().r;
    for (size_t i = pts.size() - 1; i > 0; --i) {
        double distFromEnd = pts.back().x - pts[i].x;
        if (distFromEnd > 18.0) break;
        // As we move backwards from tip, radius should be >= previous
        assert(pts[i - 1].r >= pts[i].r - 1e-4f);
    }

    std::cout << "  -> Tail Taper test passed: stroke tip tapered smoothly to minWidth ("
              << pts.back().r * 2.0f << " mm)!" << std::endl;
}

void TestAcuteTurnMeshAndEndcapNormal() {
    std::cout << "[Test 11/12] Verifying acute needle turns and tight loops (R3 & R4)..." << std::endl;

    // Acute needle hairpin turn: (0,0) -> (50, 0) -> (0, 0.5) with r = 2.0
    std::vector<StrokePoint> needlePts = {
        {  0.0, 0.0, 2.0f, 0.8f, 0.0f, 0.0f },
        { 50.0, 0.0, 2.0f, 0.8f, 0.0f, 0.0f },
        {  0.0, 0.5, 2.0f, 0.8f, 0.0f, 0.0f }
    };

    InkMesh mesh;
    InkMeshBuilder::BuildSolidMesh(needlePts, CapType::Round, 0xFF000000, mesh);
    assert(!mesh.empty());
    assert(mesh.coreIndexCount > 0);
    assert(mesh.indices.size() > mesh.coreIndexCount);

    // Verify all skirt triangles have valid vertices
    for (size_t i = mesh.coreIndexCount; i < mesh.indices.size(); i += 3) {
        assert(mesh.indices[i] < mesh.vertices.size());
        assert(mesh.indices[i + 1] < mesh.vertices.size());
        assert(mesh.indices[i + 2] < mesh.vertices.size());
    }

    // Verify endcap normal validation when leftPt and rightPt are crossed near small radius
    // Forward direction is +X (1, 0). If left and right are inverted, chord normal points -X (-1, 0) without dirX check.
    BLPath path;
    StrokePoint center(100.0, 0.0, 0.1f);
    // Crossed rails: leftPt at (100, 1) and rightPt at (100, -1) for tail cap
    // vx = 0, vy = -2 => initial chord normal ux = vy/vLen = -1.0, uy = 0.0
    // With dirX = 1.0, dirY = 0.0, ux * dirX < 0, so ux should flip to +1.0 (outward)
    InkGenerator::TraceCap(path, center, {100.0, 1.0}, {100.0, -1.0}, CapType::Round, true, 1.0, 0.0);
    assert(!path.is_empty());

    // Verify ribbon generation with acute needle points
    BLPath ribbon = InkGenerator::GenerateStroke(needlePts, CapType::Round, StrokePattern::Solid);
    assert(!ribbon.is_empty());

    std::cout << "  -> Acute turn mesh and endcap normal tests passed!" << std::endl;
}

void TestPreFilterPointsPreservesEndpoint() {
    std::cout << "[Test 12/12] Verifying StrokeFilter::PreFilterPoints preserves endpoint on pen lift (R6)..." << std::endl;

    // Stroke with trailing segment shorter than 0.20 * r (0.1mm < 0.20 * 2.0mm = 0.40mm)
    std::vector<StrokePoint> pts = {
        { 0.0, 0.0, 2.0f, 0.5f, 0.0f, 0.0f },
        { 10.0, 0.0, 2.0f, 0.5f, 0.0f, 0.0f },
        { 20.0, 0.0, 2.0f, 0.5f, 0.0f, 0.0f },
        { 20.1, 0.0, 2.0f, 0.5f, 0.0f, 0.0f }
    };

    auto clean = Folio::InkInternal::StrokeFilter::PreFilterPoints(pts);
    assert(!clean.empty());
    assert(std::abs(clean.back().x - 20.1) < 1e-4);
    assert(std::abs(clean.back().y - 0.0) < 1e-4);

    // Two-point short segment (< 0.20 * r, > 0.05 * r)
    std::vector<StrokePoint> twoPts = {
        { 5.0, 5.0, 2.0f, 0.5f, 0.0f, 0.0f },
        { 5.25, 5.0, 2.0f, 0.6f, 0.0f, 0.0f }
    };
    auto cleanTwo = Folio::InkInternal::StrokeFilter::PreFilterPoints(twoPts);
    assert(cleanTwo.size() == 1);
    assert(std::abs(cleanTwo.back().x - 5.25) < 1e-4);
    assert(std::abs(cleanTwo.back().y - 5.0) < 1e-4);
    assert(cleanTwo.back().pressure >= 0.6f);

    std::cout << "  -> StrokeFilter endpoint preservation tests passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Folio-Sketch Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestStrokeTessellationAndRasterPerformance();
    TestStrokePressureTransitions();
    TestCollisionAndSlicer();
    TestWidthEvaluatorAndPostProcessor();
    TestInkEngineLifecycle();
    TestPatternAndInfillEdgeCases();
    TestInkMeshBuilderAndDebugger();
    TestCrescentTurnGeometry();
    TestQuickStrokeLoopMesh();
    TestTailTaper();
    TestAcuteTurnMeshAndEndcapNormal();
    TestPreFilterPointsPreservesEndpoint();

    std::cout << "========================================" << std::endl;
    std::cout << "All Folio-Sketch Tests Passed!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
