#pragma once
/**
 * =========================================================================================
 * @file src/debug/stroke_scrubber.hpp
 * @brief Step 5: Time-Travel Point Scrubber, Freeze Capture, and repro114.inc Injector
 * =========================================================================================
 */

#include <imgui.h>
#include <vector>
#include <span>
#include "folio_sketch/ink_stroke_data.hpp"
#include "folio_sketch/ink_mesh.hpp"
#include "folio_sketch/ink_path.hpp"
#include "debug/raw_sensor_layer.hpp"

namespace Folio::Debug {

/**
 * @class StrokeScrubber
 * @brief Captures/freezes strokes, steps through modeled points [0 .. cursor],
 *        re-tessellates InkMesh and InkPath in real time, and injects test/repro114.inc.
 */
class StrokeScrubber {
public:
    StrokeScrubber() = default;

    /**
     * @brief Captures and freezes a stroke (raw samples + modeled points) and sets cursor to N.
     */
    void FreezeStroke(const std::vector<RawInputSample>& rawSamples,
                      const std::vector<StrokePoint>& modeledPoints,
                      const StrokeStyle& style,
                      float zoomScale = 1.0f);

    /**
     * @brief Unfreezes the inspector so new canvas strokes can be drawn freely.
     */
    void Unfreeze() noexcept { m_isFrozen = false; }

    /**
     * @brief Loads the built-in test/repro114.inc points, freezes the inspector,
     *        and positions the scrubber at the tight crescent turn defect region.
     */
    void LoadRepro114(StrokeStyle& style,
                      std::vector<RawInputSample>& outRawSamples,
                      CanvasTransform* optionalViewportToCenter = nullptr);

    /**
     * @brief Re-tessellates the scrubbed sub-stroke [0 .. m_cursor] into m_scrubbedMesh and m_scrubbedOutline.
     */
    void Retessellate(const StrokeStyle& style, float zoomScale = 1.0f);

    /**
     * @brief Renders ImGui controls for Freeze/Unfreeze, Auto-Freeze on Lift,
     *        scrubber slider (0..N), step buttons (|<, <, >, >|), hotkeys (Space, [, ]),
     *        and "Load repro114.inc".
     * @return True if scrubber state or tessellated geometry changed.
     */
    bool RenderUI(StrokeStyle& style,
                  const std::vector<RawInputSample>& currentRawSamples,
                  const std::vector<StrokePoint>& currentModeledPoints,
                  std::vector<RawInputSample>& mutableRawSamples,
                  CanvasTransform& viewport,
                  float zoomScale = 1.0f);

    /**
     * @brief Renders an on-canvas marker highlighting the current scrubber cursor vertex.
     */
    void RenderCursorHighlight(ImDrawList* drawList, const CanvasTransform& transform) const;

    // Accessors
    [[nodiscard]] bool IsFrozen() const noexcept { return m_isFrozen; }
    [[nodiscard]] bool IsAutoFreezeOnLift() const noexcept { return m_autoFreezeOnLift; }
    void SetAutoFreezeOnLift(bool autoFreeze) noexcept { m_autoFreezeOnLift = autoFreeze; }

    [[nodiscard]] int GetCursor() const noexcept { return m_cursor; }
    [[nodiscard]] int GetTotalPoints() const noexcept { return static_cast<int>(m_frozenModeledPoints.size()); }

    [[nodiscard]] std::span<const StrokePoint> GetActiveModeledSlice() const noexcept {
        if (m_frozenModeledPoints.empty() || m_cursor <= 0) {
            return {};
        }
        size_t count = std::min(static_cast<size_t>(m_cursor), m_frozenModeledPoints.size());
        return std::span<const StrokePoint>(m_frozenModeledPoints.data(), count);
    }

    [[nodiscard]] std::vector<StrokePoint>& GetMutableFrozenPoints() noexcept { return m_frozenModeledPoints; }
    [[nodiscard]] const std::vector<StrokePoint>& GetFrozenPoints() const noexcept { return m_frozenModeledPoints; }
    [[nodiscard]] const InkMesh& GetScrubbedMesh() const noexcept { return m_scrubbedMesh; }
    [[nodiscard]] const InkPath& GetScrubbedOutline() const noexcept { return m_scrubbedOutline; }

private:
    bool m_isFrozen = false;
    bool m_autoFreezeOnLift = true;
    int  m_cursor = 0;

    std::vector<RawInputSample> m_frozenRawSamples;
    std::vector<StrokePoint>    m_frozenModeledPoints;
    InkMesh                     m_scrubbedMesh;
    InkPath                     m_scrubbedOutline;
};

} // namespace Folio::Debug
