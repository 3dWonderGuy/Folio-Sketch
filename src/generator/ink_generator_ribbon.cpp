#include "folio_sketch/ink_generator.hpp"
#include "ink_generator_internal.hpp"
#include "geometry/ink_stroke_filter.hpp"
#include "geometry/ink_geometry_utils.hpp"
#include "ink_ribbon_corner.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace Folio::InkInternal;

namespace {

// =============================================================================
// CORE SINGLE RIBBON BUILDER (Open ribbon or closed polygon loop)
// =============================================================================

BLPath BuildSingleRibbon(const std::vector<StrokePoint>& cleanPts,
                         CapType capType,
                         bool isClosedPolygon,
                         std::vector<Point2D>* outLeftPts = nullptr,
                         std::vector<Point2D>* outRightPts = nullptr,
                         std::vector<size_t>* outRetrogradeIndices = nullptr) {
    BLPath path;
    if (cleanPts.empty()) return path;
    if (cleanPts.size() == 1) return InkGenerator::TraceCap(cleanPts[0], capType);

    const size_t N = cleanPts.size();
    bool isClosed = isClosedPolygon && (N >= 4) &&
                    (std::hypot(cleanPts.back().x - cleanPts.front().x,
                                cleanPts.back().y - cleanPts.front().y) < 1e-4);
    const size_t M = isClosed ? (N - 1) : N;

    std::vector<SegmentInfo> segs = ComputeSegments(cleanPts);
    if (segs.empty()) return path;

    std::vector<Point2D> leftPts;
    std::vector<Point2D> rightPts;
    leftPts.reserve(N * 2);
    rightPts.reserve(N * 2);

    if (isClosed) {
        // ---------------------------------------------------------------------
        // CLOSED POLYGON LOOP (Seamless ring without terminal endcaps)
        // ---------------------------------------------------------------------
        for (size_t i = 0; i < M; ++i) {
            const auto& sIn  = (i == 0) ? segs[M - 1] : segs[i - 1];
            const auto& sOut = segs[i];
            ProcessCorner(cleanPts[i], capType, sIn, sOut, leftPts, rightPts);
        }

        PruneSwallowtailLoops(leftPts);
        PruneSwallowtailLoops(rightPts);

        if (leftPts.empty() || rightPts.empty()) return path;

        // Outer contour (CW)
        path.move_to(leftPts[0].x, leftPts[0].y);
        for (size_t i = 1; i < leftPts.size(); ++i) {
            path.line_to(leftPts[i].x, leftPts[i].y);
        }
        path.close();

        // Inner contour in reverse winding direction (CCW)
        path.move_to(rightPts.back().x, rightPts.back().y);
        for (size_t i = rightPts.size() - 1; i > 0; --i) {
            path.line_to(rightPts[i - 1].x, rightPts[i - 1].y);
        }
        path.close();

        if (outRetrogradeIndices) {
            outRetrogradeIndices->clear();
        }

        if (outLeftPts) *outLeftPts = leftPts;
        if (outRightPts) *outRightPts = rightPts;
        return path;
    }

    // -------------------------------------------------------------------------
    // OPEN STROKE RIBBON (Honors endcaps at P0 and P_{N-1})
    // -------------------------------------------------------------------------

    // Start point P0
    {
        Point2D offL0, offR0;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts[0].r), segs[0].nx, segs[0].ny, offL0, offR0);
        leftPts.push_back({ cleanPts[0].x + offL0.x, cleanPts[0].y + offL0.y });
        rightPts.push_back({ cleanPts[0].x + offR0.x, cleanPts[0].y + offR0.y });
    }

    // Interior vertices Pi (i = 1 .. N - 2)
    for (size_t i = 1; i < N - 1; ++i) {
        ProcessCorner(cleanPts[i], capType, segs[i - 1], segs[i], leftPts, rightPts);
    }

    // End point P_{N-1}
    {
        const auto& lastSeg = segs.back();
        Point2D offL_end, offR_end;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts.back().r), lastSeg.nx, lastSeg.ny, offL_end, offR_end);
        Point2D endL = { cleanPts.back().x + offL_end.x, cleanPts.back().y + offL_end.y };
        Point2D endR = { cleanPts.back().x + offR_end.x, cleanPts.back().y + offR_end.y };

        PushTerminalCap(leftPts, endL, lastSeg.ux, lastSeg.uy);
        PushTerminalCap(rightPts, endR, lastSeg.ux, lastSeg.uy);
    }

    // Prune local swallowtail self-intersection loops caused by turn radius R < r
    PruneSwallowtailLoops(leftPts);
    PruneSwallowtailLoops(rightPts);

    if (outRetrogradeIndices) {
        outRetrogradeIndices->clear();
        auto leftRetro = DetectRetrogradeSteps(leftPts, segs, /*isRight=*/false);
        auto rightRetro = DetectRetrogradeSteps(rightPts, segs, /*isRight=*/true);
        outRetrogradeIndices->insert(outRetrogradeIndices->end(), leftRetro.begin(), leftRetro.end());
        outRetrogradeIndices->insert(outRetrogradeIndices->end(), rightRetro.begin(), rightRetro.end());
    }

    if (leftPts.empty() || rightPts.empty()) {
        if (outLeftPts) *outLeftPts = leftPts;
        if (outRightPts) *outRightPts = rightPts;
        return path;
    }

    // Extrude single continuous closed contour with head/tail endcaps
    path.move_to(leftPts[0].x, leftPts[0].y);

    for (size_t i = 1; i < leftPts.size(); ++i) {
        path.line_to(leftPts[i].x, leftPts[i].y);
    }

    // Tail Cap: leftPts.back() -> rightPts.back() around cleanPts.back()
    const auto& lastSeg = segs.back();
    InkGenerator::TraceCap(path, cleanPts.back(), leftPts.back(), rightPts.back(), capType, /*isTail=*/true, lastSeg.ux, lastSeg.uy);

    if (rightPts.size() > 1) {
        for (size_t i = rightPts.size() - 1; i > 0; --i) {
            path.line_to(rightPts[i - 1].x, rightPts[i - 1].y);
        }
    }

    // Head Cap: rightPts[0] -> leftPts[0] around cleanPts.front()
    InkGenerator::TraceCap(path, cleanPts.front(), leftPts[0], rightPts[0], capType, /*isTail=*/false, -segs[0].ux, -segs[0].uy);

    path.close();

    if (outLeftPts) *outLeftPts = leftPts;
    if (outRightPts) *outRightPts = rightPts;
    return path;
}

void RecordDebugInfo(const std::vector<StrokePoint>& rawPts,
                     const std::vector<StrokePoint>& cleanPts,
                     const std::vector<Point2D>& leftRail,
                     const std::vector<Point2D>& rightRail,
                     const std::vector<size_t>& retroIndices,
                     const BLPath& builtPath,
                     CapType capType,
                     bool isClosedPolygon) {
    auto& dbg = Folio::InkInternal::GetMutableDebugInfo();
    dbg = StrokeDebugInfo{};
    dbg.rawInputPts = rawPts;
    dbg.cleanPts = cleanPts;
    dbg.leftRail = leftRail;
    dbg.rightRail = rightRail;
    dbg.retrogradeIndices = retroIndices;
    dbg.builtPath = builtPath;
    dbg.generatedPath = builtPath;
    dbg.capType = capType;
    dbg.isClosedPolygon = isClosedPolygon;
    DetectSelfIntersections(leftRail, rightRail, isClosedPolygon, dbg.selfIntersections);
    dbg.hasDefects = (!dbg.selfIntersections.empty() || !dbg.retrogradeIndices.empty());
}

} // namespace

// =============================================================================
// CONTINUOUS SOLID RIBBON TESSELLATION (Public Entry Point)
// =============================================================================

BLPath InkGenerator::BuildSolidRibbon(const std::vector<StrokePoint>& pts, CapType capType, bool isClosedPolygon) {
    bool isDbg = InkGenerator::IsDebugCaptureEnabled();

    if (pts.empty()) {
        if (isDbg) {
            auto& dbg = Folio::InkInternal::GetMutableDebugInfo();
            dbg = StrokeDebugInfo{};
            dbg.capType = capType;
            dbg.isClosedPolygon = isClosedPolygon;
        }
        return BLPath{};
    }

    if (pts.size() == 1) {
        BLPath single = TraceCap(pts[0], capType);
        if (isDbg) {
            RecordDebugInfo(pts, pts, {}, {}, {}, single, capType, isClosedPolygon);
        }
        return single;
    }

    // 1. Point Pre-Filtering & Decimation
    std::vector<StrokePoint> cleanPts = StrokeFilter::PreFilterPoints(pts);

    if (cleanPts.size() <= 1) {
        BLPath single = TraceCap(cleanPts.empty() ? pts[0] : cleanPts[0], capType);
        if (isDbg) {
            RecordDebugInfo(pts, cleanPts, {}, {}, {}, single, capType, isClosedPolygon);
        }
        return single;
    }

    // 2. Regularize radius rate-of-change: |dr/ds| <= maxSlope
    StrokeFilter::RegularizeRadiusSlope(cleanPts);

    // 3. Closed Polygon Handling
    if (isClosedPolygon) {
        if (cleanPts.size() >= 3) {
            if (std::hypot(cleanPts.back().x - cleanPts.front().x,
                           cleanPts.back().y - cleanPts.front().y) >= 1e-4) {
                cleanPts.push_back(cleanPts.front());
            }
        }
        std::vector<Point2D> leftRail, rightRail;
        std::vector<size_t> retroIndices;
        BLPath resPath = BuildSingleRibbon(cleanPts, capType, true,
                                           isDbg ? &leftRail : nullptr,
                                           isDbg ? &rightRail : nullptr,
                                           isDbg ? &retroIndices : nullptr);

        if (isDbg) {
            RecordDebugInfo(pts, cleanPts, leftRail, rightRail, retroIndices, resPath, capType, true);
        }
        return resPath;
    }

    // 4. Hairpin Turn Splitting (Open Strokes Only)
    std::vector<size_t> splitIndices = StrokeFilter::FindHairpinTurns(cleanPts);

    if (splitIndices.empty()) {
        std::vector<Point2D> leftRail, rightRail;
        std::vector<size_t> retroIndices;
        BLPath resPath = BuildSingleRibbon(cleanPts, capType, false,
                                           isDbg ? &leftRail : nullptr,
                                           isDbg ? &rightRail : nullptr,
                                           isDbg ? &retroIndices : nullptr);

        if (isDbg) {
            RecordDebugInfo(pts, cleanPts, leftRail, rightRail, retroIndices, resPath, capType, false);
        }
        return resPath;
    }

    // Partition at hairpin reversals into sub-ribbons
    BLPath combinedPath;
    std::vector<Point2D> combinedLeft, combinedRight;
    std::vector<size_t> combinedRetro;
    size_t subStart = 0;

    for (size_t splitIdx : splitIndices) {
        if (splitIdx > subStart) {
            std::vector<StrokePoint> subPts(cleanPts.begin() + subStart, cleanPts.begin() + splitIdx + 1);
            std::vector<Point2D> subLeft, subRight;
            std::vector<size_t> subRetro;
            BLPath subRibbon = BuildSingleRibbon(subPts, capType, false,
                                                 isDbg ? &subLeft : nullptr,
                                                 isDbg ? &subRight : nullptr,
                                                 isDbg ? &subRetro : nullptr);
            if (!subRibbon.is_empty()) {
                combinedPath.add_path(subRibbon);
            }
            if (isDbg) {
                combinedLeft.insert(combinedLeft.end(), subLeft.begin(), subLeft.end());
                combinedRight.insert(combinedRight.end(), subRight.begin(), subRight.end());
                combinedRetro.insert(combinedRetro.end(), subRetro.begin(), subRetro.end());
            }
            subStart = splitIdx;
        }
    }

    if (subStart < cleanPts.size() - 1) {
        std::vector<StrokePoint> subPts(cleanPts.begin() + subStart, cleanPts.end());
        std::vector<Point2D> subLeft, subRight;
        std::vector<size_t> subRetro;
        BLPath subRibbon = BuildSingleRibbon(subPts, capType, false,
                                             isDbg ? &subLeft : nullptr,
                                             isDbg ? &subRight : nullptr,
                                             isDbg ? &subRetro : nullptr);
        if (!subRibbon.is_empty()) {
            combinedPath.add_path(subRibbon);
        }
        if (isDbg) {
            combinedLeft.insert(combinedLeft.end(), subLeft.begin(), subLeft.end());
            combinedRight.insert(combinedRight.end(), subRight.begin(), subRight.end());
            combinedRetro.insert(combinedRetro.end(), subRetro.begin(), subRetro.end());
        }
    }

    if (isDbg) {
        RecordDebugInfo(pts, cleanPts, combinedLeft, combinedRight, combinedRetro, combinedPath, capType, false);
    }

    return combinedPath;
}
