/**
 * =========================================================================================
 * @file src/engine/post_processing/shape_recognizer.cpp
 * @brief Geometric Classification, Fitting, and Synthetic Stroke Point Generation
 * =========================================================================================
 */

#include "engine/post_processing/shape_recognizer.hpp"

#include <algorithm>
#include <cmath>

namespace Folio {

ShapeRecognitionResult ShapeRecognizer::ClassifyAndFit(
    const std::vector<StrokePoint>& points,
    const InkingConfig& config)
{
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

    // Check closed loop condition:
    // Allow either the fixed mm gap OR a gap up to 10% of the stroke's total arc length,
    // so large zoomed-out shapes (e.g. a house-scale circle) still close cleanly.
    const float effectiveClosureGap = std::max(snap.loopClosureGapMm, totalArcLength * 0.10f);
    bool isClosed = (endToEndDist <= effectiveClosureGap) && (totalArcLength > snap.loopClosureGapMm * 2.0f);

    // 1. OPEN STROKE: Test Straight Line
    if (!isClosed) {
        // A perfectly straight stroke has a path length equal to the straight distance
        // between its endpoints. Any wobble or curve makes the path longer.
        //   totalArcLength = length of the path the pen actually traveled
        //   endToEndDist   = straight-line distance from first to last point
        // The tuner (lineStraightnessRatio) is how much longer the path may be, as a fraction
        // of the endpoint distance (0.02 = path may be up to 2% longer than the straight chord).
        // Being a ratio, it behaves the same for short and long strokes.
        float lineDx = pEnd.x - pStart.x;
        float lineDy = pEnd.y - pStart.y;

        if (endToEndDist > 1e-4f) {
            float excessRatio = (totalArcLength - endToEndDist) / endToEndDist;

            if (excessRatio <= snap.lineStraightnessRatio) {
                result.type = RecognizedShapeType::Line;
                result.isClosed = false;
                result.confidence = std::clamp(1.0f - (excessRatio / std::max(1e-4f, snap.lineStraightnessRatio)), 0.0f, 1.0f);

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
        }

        return result; // Open non-straight curve (or degenerate zero-length stroke)
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

    // Resample to 64 equal-arc-length points so slow/dense regions don't bias any fit.
    constexpr size_t kResampleCount = 64;
    std::vector<Point2D> resampled;
    resampled.reserve(kResampleCount);
    {
        const double step = static_cast<double>(totalArcLength) / static_cast<double>(kResampleCount - 1);
        resampled.emplace_back(points[0].x, points[0].y);
        double accum = 0.0;
        double nextTarget = step;
        for (size_t i = 1; i < points.size() && resampled.size() < kResampleCount; ++i) {
            const double dx = points[i].x - points[i - 1].x;
            const double dy = points[i].y - points[i - 1].y;
            const double segLen = std::hypot(dx, dy);
            while (accum + segLen >= nextTarget && resampled.size() < kResampleCount) {
                const double t = (segLen > 1e-9) ? (nextTarget - accum) / segLen : 0.0;
                resampled.emplace_back(points[i - 1].x + t * dx, points[i - 1].y + t * dy);
                nextTarget += step;
            }
            accum += segLen;
        }
        while (resampled.size() < kResampleCount) {
            resampled.emplace_back(points.back().x, points.back().y);
        }
    }

    // --- Candidate 1: Polygon (Triangle, Square, Rectangle, Diamond, Quad) ---
    // Use pure distance-based RDP (maxTurnDeg = 180) so slightly rounded corners or bowed
    // edges don't spawn extra vertices, then prune near-collinear vertices.
    std::vector<StrokePoint> polygonVertices = StrokePostProcessor::Simplify(
        points,
        /*toleranceMm=*/std::max(1.0f, 0.05f * std::max(bboxW, bboxH)),
        /*maxTurnDeg=*/180.0f,
        /*radiusTolerance=*/10.0f);

    // Remove redundant duplicate endpoint in closed loop
    if (polygonVertices.size() >= 2) {
        float d = std::hypot(polygonVertices.back().x - polygonVertices.front().x,
                             polygonVertices.back().y - polygonVertices.front().y);
        if (d <= effectiveClosureGap) {
            polygonVertices.pop_back();
        }
    }

    // Prune vertices whose interior angle is nearly straight (not a real corner)
    bool pruned = true;
    while (pruned && polygonVertices.size() > 3) {
        pruned = false;
        const size_t m = polygonVertices.size();
        for (size_t i = 0; i < m; ++i) {
            const auto& prev = polygonVertices[(i + m - 1) % m];
            const auto& curr = polygonVertices[i];
            const auto& next = polygonVertices[(i + 1) % m];
            double ax = prev.x - curr.x, ay = prev.y - curr.y;
            double bx = next.x - curr.x, by = next.y - curr.y;
            double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
            if (la > 1e-4 && lb > 1e-4) {
                double cosTheta = std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0);
                float interiorDeg = static_cast<float>(std::acos(cosTheta) * 180.0 / 3.14159265358979323846);
                // Turn angle = 180 - interiorDeg; if turn < threshold, it's not a sharp corner
                if ((180.0f - interiorDeg) < snap.polygonCornerAngleThresholdDeg) {
                    polygonVertices.erase(polygonVertices.begin() + static_cast<ptrdiff_t>(i));
                    pruned = true;
                    break;
                }
            }
        }
    }

    const size_t cornerCount = polygonVertices.size();
    float polyRmsErr = 1e9f;
    RecognizedShapeType polyType = RecognizedShapeType::None;

    if (cornerCount == 3 || cornerCount == 4) {
        // Compute normalized RMS distance from resampled points to nearest polygon edge
        const float charSize = std::max(0.1f, 0.5f * (bboxW + bboxH));
        double sumSq = 0.0;
        for (const auto& p : resampled) {
            double minEdgeDistSq = 1e18;
            for (size_t e = 0; e < cornerCount; ++e) {
                const auto& a = polygonVertices[e];
                const auto& b = polygonVertices[(e + 1) % cornerCount];
                const double ex = b.x - a.x;
                const double ey = b.y - a.y;
                const double lenSq = ex * ex + ey * ey;
                double t = 0.0;
                if (lenSq > 1e-9) {
                    t = std::clamp(((p.x - a.x) * ex + (p.y - a.y) * ey) / lenSq, 0.0, 1.0);
                }
                const double dx = p.x - (a.x + t * ex);
                const double dy = p.y - (a.y + t * ey);
                minEdgeDistSq = std::min(minEdgeDistSq, dx * dx + dy * dy);
            }
            sumSq += minEdgeDistSq;
        }
        polyRmsErr = static_cast<float>(std::sqrt(sumSq / static_cast<double>(resampled.size()))) / charSize;

        if (cornerCount == 3) {
            polyType = RecognizedShapeType::Triangle;
        } else {
            // 4-Corner Promotion Hierarchy:
            //   Edge case: Trapezoid (Quadrilateral) if one pair of opposite sides is parallel
            //              AND one side is much shorter (< 75%) than its opposite.
            //   Base:      Parallelogram (Quadrilateral) / Diamond (equal sides, non-90 deg)
            //   Promote 1: Rectangle (corners within rectAngleToleranceDeg of 90 deg)
            //   Promote 2: Square (Rectangle with equal sides within circleAspectTolerance)
            float edgeLen[4];
            double edgeDx[4], edgeDy[4];
            float minEdge = 1e9f, maxEdge = 0.0f;
            for (size_t e = 0; e < 4; ++e) {
                const auto& a = polygonVertices[e];
                const auto& b = polygonVertices[(e + 1) % 4];
                edgeDx[e] = b.x - a.x;
                edgeDy[e] = b.y - a.y;
                edgeLen[e] = static_cast<float>(std::hypot(edgeDx[e], edgeDy[e]));
                minEdge = std::min(minEdge, edgeLen[e]);
                maxEdge = std::max(maxEdge, edgeLen[e]);
            }
            float edgeAspectDiff = (maxEdge > 1e-4f) ? (maxEdge - minEdge) / maxEdge : 1.0f;
            bool equalSides = (edgeAspectDiff <= snap.circleAspectTolerance);

            // Check corner angles (how close to 90 degrees)
            float maxAngleDevDeg = 0.0f;
            for (size_t e = 0; e < 4; ++e) {
                const auto& prev = polygonVertices[(e + 3) % 4];
                const auto& curr = polygonVertices[e];
                const auto& next = polygonVertices[(e + 1) % 4];
                double ax = prev.x - curr.x, ay = prev.y - curr.y;
                double bx = next.x - curr.x, by = next.y - curr.y;
                double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
                if (la > 1e-4 && lb > 1e-4) {
                    double cosTheta = std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0);
                    float deg = static_cast<float>(std::acos(cosTheta) * 180.0 / 3.14159265358979323846);
                    maxAngleDevDeg = std::max(maxAngleDevDeg, std::abs(deg - 90.0f));
                }
            }
            bool rightAngles = (maxAngleDevDeg <= snap.rectAngleToleranceDeg);

            // Trapezoid edge-case check: one pair of opposite sides parallel (< 12 deg)
            // and clearly unequal (< 75% length ratio)
            auto IsTrapezoidPair = [&](size_t e0, size_t e1) -> bool {
                if (edgeLen[e0] < 1e-4f || edgeLen[e1] < 1e-4f) return false;
                // Opposite edges in a loop point in opposite directions, so negate e1
                double dot = -(edgeDx[e0] * edgeDx[e1] + edgeDy[e0] * edgeDy[e1]);
                double cosP = std::clamp(dot / (edgeLen[e0] * edgeLen[e1]), -1.0, 1.0);
                float angleDeg = static_cast<float>(std::acos(cosP) * 180.0 / 3.14159265358979323846);
                float lenRatio = std::min(edgeLen[e0], edgeLen[e1]) / std::max(edgeLen[e0], edgeLen[e1]);
                return (angleDeg <= 12.0f) && (lenRatio < 0.75f);
            };
            bool isTrapezoid = IsTrapezoidPair(0, 2) || IsTrapezoidPair(1, 3);

            if (rightAngles && !isTrapezoid) {
                polyType = equalSides ? RecognizedShapeType::Square : RecognizedShapeType::Rectangle;
            } else if (equalSides && !isTrapezoid) {
                polyType = RecognizedShapeType::Diamond;
            } else {
                polyType = RecognizedShapeType::Quadrilateral;
            }
        }
    }

    // --- Kåsa Least-Squares Circle Fit ---
    // Solves for center (kCx, kCy) and radius kR minimizing sum((x-cx)^2 + (y-cy)^2 - R^2)^2
    double meanX = 0.0, meanY = 0.0;
    for (const auto& p : resampled) {
        meanX += p.x;
        meanY += p.y;
    }
    meanX /= static_cast<double>(resampled.size());
    meanY /= static_cast<double>(resampled.size());

    double suu = 0.0, suv = 0.0, svv = 0.0;
    double suuu = 0.0, suvv = 0.0, svvv = 0.0, svuu = 0.0;
    for (const auto& p : resampled) {
        const double u = p.x - meanX;
        const double v = p.y - meanY;
        const double uu = u * u;
        const double vv = v * v;
        suu += uu;
        suv += u * v;
        svv += vv;
        suuu += uu * u;
        suvv += u * vv;
        svvv += vv * v;
        svuu += vv * u;
    }

    const double det = suu * svv - suv * suv;
    float circleRmsErr = 1e9f;
    float kasaCx = static_cast<float>(meanX);
    float kasaCy = static_cast<float>(meanY);
    float kasaR = 0.0f;

    if (std::abs(det) > 1e-9) {
        const double rhs1 = 0.5 * (suuu + suvv);
        const double rhs2 = 0.5 * (svvv + svuu);
        const double uc = (rhs1 * svv - rhs2 * suv) / det;
        const double vc = (suu * rhs2 - suv * rhs1) / det;
        kasaCx = static_cast<float>(meanX + uc);
        kasaCy = static_cast<float>(meanY + vc);
        kasaR  = static_cast<float>(std::sqrt(uc * uc + vc * vc + (suu + svv) / static_cast<double>(resampled.size())));

        if (kasaR > 1e-3f) {
            double sumSq = 0.0;
            for (const auto& p : resampled) {
                const double d = std::hypot(p.x - kasaCx, p.y - kasaCy) - kasaR;
                sumSq += d * d;
            }
            circleRmsErr = static_cast<float>(std::sqrt(sumSq / static_cast<double>(resampled.size()))) / kasaR;
        }
    }

    // --- PCA Rotated Ellipse Fit ---
    // Covariance matrix of resampled points gives orientation angle and semi-axes (ra, rb).
    const double invN = 1.0 / static_cast<double>(resampled.size());
    const double cxx = suu * invN;
    const double cxy = suv * invN;
    const double cyy = svv * invN;

    const double trace = cxx + cyy;
    const double diff  = cxx - cyy;
    const double disc  = std::hypot(diff, 2.0 * cxy);
    const double lambda1 = 0.5 * (trace + disc);
    const double lambda2 = std::max(1e-9, 0.5 * (trace - disc));

    // For a uniform ellipse boundary, Var = 0.5 * r^2, so r = sqrt(2 * lambda)
    const float pcaRa = static_cast<float>(std::sqrt(2.0 * lambda1));
    const float pcaRb = static_cast<float>(std::sqrt(2.0 * lambda2));
    const float pcaAngle = 0.5f * static_cast<float>(std::atan2(2.0 * cxy, diff));

    float ellipseRmsErr = 1e9f;
    if (pcaRa > 1e-3f && pcaRb > 1e-3f) {
        const float cosA = std::cos(-pcaAngle);
        const float sinA = std::sin(-pcaAngle);
        double sumSq = 0.0;
        for (const auto& p : resampled) {
            const float dx = static_cast<float>(p.x - meanX);
            const float dy = static_cast<float>(p.y - meanY);
            const float lx = dx * cosA - dy * sinA;
            const float ly = dx * sinA + dy * cosA;
            const float normDist = std::hypot(lx / pcaRa, ly / pcaRb);
            const double err = static_cast<double>(normDist - 1.0f);
            sumSq += err * err;
        }
        ellipseRmsErr = static_cast<float>(std::sqrt(sumSq * invN));
    }

    // --- Score all candidates by normalized confidence [0, 1] ---
    // Each shape is judged against its own dedicated tuner ratio.
    // The highest-confidence shape wins, provided it leads the runner-up by at least winnerMarginRatio.
    struct Candidate {
        RecognizedShapeType type = RecognizedShapeType::None;
        float confidence = -1.0f;
    };

    Candidate best;
    Candidate secondBest;

    auto SubmitCandidate = [&](RecognizedShapeType t, float err, float tol) {
        if (tol <= 1e-4f || err > tol) return;
        float conf = std::clamp(1.0f - (err / tol), 0.0f, 1.0f);
        if (conf > best.confidence) {
            secondBest = best;
            best = { t, conf };
        } else if (conf > secondBest.confidence) {
            secondBest = { t, conf };
        }
    };

    if (polyType != RecognizedShapeType::None) {
        float polyTol = 0.09f;
        switch (polyType) {
            case RecognizedShapeType::Triangle:      polyTol = snap.triangleFitToleranceRatio; break;
            case RecognizedShapeType::Square:        polyTol = snap.squareFitToleranceRatio; break;
            case RecognizedShapeType::Rectangle:     polyTol = snap.rectangleFitToleranceRatio; break;
            case RecognizedShapeType::Diamond:       polyTol = snap.diamondFitToleranceRatio; break;
            case RecognizedShapeType::Quadrilateral: polyTol = snap.quadFitToleranceRatio; break;
            default: break;
        }
        SubmitCandidate(polyType, polyRmsErr, polyTol);
    }

    const float aspectDiff = (pcaRa > 1e-4f) ? std::abs(pcaRa - pcaRb) / pcaRa : 1.0f;
    if (aspectDiff <= snap.circleAspectTolerance) {
        // Near-circular loop: promote to Circle only (don't let Ellipse compete against it)
        SubmitCandidate(RecognizedShapeType::Circle, circleRmsErr, snap.circleFitToleranceRatio);
    } else {
        SubmitCandidate(RecognizedShapeType::Ellipse, ellipseRmsErr, snap.ellipseFitToleranceRatio);
    }

    // Winner must beat the runner-up by at least winnerMarginRatio (e.g. 10%)
    if (best.type != RecognizedShapeType::None) {
        if (secondBest.type != RecognizedShapeType::None &&
            (best.confidence - secondBest.confidence) < snap.winnerMarginRatio) {
            return result; // Ambiguous tie: leave as freehand
        }

        result.type = best.type;
        result.confidence = best.confidence;

        // Helper: snap an angle (radians) to the nearest 45-degree multiple if within axisSnapAngleDeg
        auto SnapAngle = [&](float angleRad) -> float {
            if (snap.axisSnapAngleDeg <= 0.01f) return angleRad;
            constexpr float kPiOver4 = 0.78539816339744830962f;
            float snapped = std::round(angleRad / kPiOver4) * kPiOver4;
            float diffDeg = std::abs(angleRad - snapped) * (180.0f / 3.14159265358979323846f);
            return (diffDeg <= snap.axisSnapAngleDeg) ? snapped : angleRad;
        };

        if (best.type == RecognizedShapeType::Circle) {
            SampleEllipse(kasaCx, kasaCy, kasaR, kasaR, 0.0f, avgRadius, result.syntheticPoints);
        } else if (best.type == RecognizedShapeType::Ellipse) {
            SampleEllipse(static_cast<float>(meanX), static_cast<float>(meanY), pcaRa, pcaRb, SnapAngle(pcaAngle), avgRadius, result.syntheticPoints);
        } else if (best.type == RecognizedShapeType::Square || best.type == RecognizedShapeType::Rectangle) {
            // Generate a mathematically perfect Rectangle or Square aligned to the drawn orientation
            float angle = SnapAngle(static_cast<float>(std::atan2(
                polygonVertices[1].y - polygonVertices[0].y,
                polygonVertices[1].x - polygonVertices[0].x)));
            float edge0 = static_cast<float>(std::hypot(polygonVertices[1].x - polygonVertices[0].x,
                                                        polygonVertices[1].y - polygonVertices[0].y));
            float edge1 = static_cast<float>(std::hypot(polygonVertices[2].x - polygonVertices[1].x,
                                                        polygonVertices[2].y - polygonVertices[1].y));
            float edge2 = static_cast<float>(std::hypot(polygonVertices[3].x - polygonVertices[2].x,
                                                        polygonVertices[3].y - polygonVertices[2].y));
            float edge3 = static_cast<float>(std::hypot(polygonVertices[0].x - polygonVertices[3].x,
                                                        polygonVertices[0].y - polygonVertices[3].y));
            float w = 0.5f * (edge0 + edge2);
            float h = 0.5f * (edge1 + edge3);
            if (best.type == RecognizedShapeType::Square) {
                float side = 0.5f * (w + h);
                w = side;
                h = side;
            }
            float qcx = 0.25f * static_cast<float>(polygonVertices[0].x + polygonVertices[1].x + polygonVertices[2].x + polygonVertices[3].x);
            float qcy = 0.25f * static_cast<float>(polygonVertices[0].y + polygonVertices[1].y + polygonVertices[2].y + polygonVertices[3].y);
            float cosA = std::cos(angle), sinA = std::sin(angle);
            float hw = w * 0.5f, hh = h * 0.5f;
            std::vector<StrokePoint> perfectQuad;
            perfectQuad.reserve(4);
            const float sx[4] = { -hw,  hw,  hw, -hw };
            const float sy[4] = { -hh, -hh,  hh,  hh };
            for (int k = 0; k < 4; ++k) {
                perfectQuad.emplace_back(qcx + sx[k] * cosA - sy[k] * sinA,
                                         qcy + sx[k] * sinA + sy[k] * cosA,
                                         avgRadius);
            }
            SampleClosedPolygon(perfectQuad, avgRadius, result.syntheticPoints);
        } else {
            SampleClosedPolygon(polygonVertices, avgRadius, result.syntheticPoints);
        }
        return result;
    }

    return result;
}

void ShapeRecognizer::SampleClosedPolygon(
    const std::vector<StrokePoint>& vertices,
    float radius,
    std::vector<StrokePoint>& outPoints)
{
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

void ShapeRecognizer::SampleEllipse(
    float cx, float cy, float rx, float ry,
    float angleRad,
    float radius,
    std::vector<StrokePoint>& outPoints)
{
    constexpr size_t kTotalSamples = 64;
    outPoints.reserve(kTotalSamples + 1);
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float cosA = std::cos(angleRad);
    const float sinA = std::sin(angleRad);

    for (size_t i = 0; i <= kTotalSamples; ++i) {
        float theta = (static_cast<float>(i) / static_cast<float>(kTotalSamples)) * kTwoPi;
        float lx = rx * std::cos(theta);
        float ly = ry * std::sin(theta);
        float x = cx + lx * cosA - ly * sinA;
        float y = cy + lx * sinA + ly * cosA;
        outPoints.emplace_back(x, y, radius);
    }
}

} // namespace Folio
