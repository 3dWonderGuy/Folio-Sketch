#pragma once
/**
 * =========================================================================================
 * @file include/folio_sketch/core/stroke_presets.hpp
 * @brief Core Inking Stroke Presets & Turnkey Tool Styles
 * =========================================================================================
 *
 * Provides ready-to-use factory constructors for standard writing, drafting, and artistic
 * inking tools. Each preset preconfigures baseline width, color, pressure dynamics, velocity
 * thinning, tilt shading, and patterns for Folio-Sketch.
 */

#include "folio_sketch/core/ink_stroke_data.hpp"

namespace Folio {

/**
 * @struct StrokePresets
 * @brief Factory helpers providing ready-to-use StrokeStyle configurations.
 */
struct StrokePresets {
    /**
     * @brief Crisp, isotropic ballpoint pen with fine line width and subtle pressure modulation.
     */
    [[nodiscard]] static constexpr StrokeStyle BallpointPen(
        float widthMm = 0.40f,
        ColorRgba color = ColorRgba(24, 26, 32, 255)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Solid;
        s.usePressure = true;
        s.useVelocity = false;
        s.useTilt     = false;
        return s;
    }

    /**
     * @brief Wide, opaque felt-tip marker with uniform width (pressure independent).
     */
    [[nodiscard]] static constexpr StrokeStyle Marker(
        float widthMm = 1.80f,
        ColorRgba color = ColorRgba(0, 120, 215, 255)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Solid;
        s.usePressure = false;
        s.useVelocity = false;
        s.useTilt     = false;
        return s;
    }

    /**
     * @brief Classic fountain pen: thick when writing slowly, thins out dynamically on quick flicks.
     */
    [[nodiscard]] static constexpr StrokeStyle FountainPen(
        float widthMm = 0.75f,
        ColorRgba color = ColorRgba(15, 25, 90, 255)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Solid;
        s.usePressure = true;
        s.useVelocity = true;
        s.useTilt     = false;
        return s;
    }

    /**
     * @brief Broad translucent highlighter with uniform width.
     */
    [[nodiscard]] static constexpr StrokeStyle Highlighter(
        float widthMm = 3.60f,
        ColorRgba color = ColorRgba(255, 235, 59, 100)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Solid;
        s.usePressure = false;
        s.useVelocity = false;
        s.useTilt     = false;
        return s;
    }

    /**
     * @brief Graphite shading pencil: sensitive to contact pressure with tilt-responsive side shading.
     */
    [[nodiscard]] static constexpr StrokeStyle ShadingPencil(
        float widthMm = 0.60f,
        ColorRgba color = ColorRgba(55, 55, 62, 230)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Solid;
        s.usePressure = true;
        s.useVelocity = false;
        s.useTilt     = true;
        return s;
    }

    /**
     * @brief Engineering dashed drafting line.
     */
    [[nodiscard]] static constexpr StrokeStyle DashedLine(
        float widthMm = 0.50f,
        ColorRgba color = ColorRgba(24, 26, 32, 255)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Dashed;
        s.usePressure = false;
        s.useVelocity = false;
        s.useTilt     = false;
        return s;
    }

    /**
     * @brief Fine dotted annotation line.
     */
    [[nodiscard]] static constexpr StrokeStyle DottedLine(
        float widthMm = 0.50f,
        ColorRgba color = ColorRgba(24, 26, 32, 255)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::Dotted;
        s.usePressure = false;
        s.useVelocity = false;
        s.useTilt     = false;
        return s;
    }

    /**
     * @brief Architectural center-axis dash-dot line.
     */
    [[nodiscard]] static constexpr StrokeStyle DashDotLine(
        float widthMm = 0.50f,
        ColorRgba color = ColorRgba(24, 26, 32, 255)) noexcept
    {
        StrokeStyle s{};
        s.baseWidth   = widthMm;
        s.color       = color;
        s.pattern     = StrokePattern::DashDot;
        s.usePressure = false;
        s.useVelocity = false;
        s.useTilt     = false;
        return s;
    }
};

} // namespace Folio
