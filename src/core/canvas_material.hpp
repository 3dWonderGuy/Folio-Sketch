#pragma once
/**
 * =========================================================================================
 * @file src/core/canvas_material.hpp
 * @brief Physical Canvas Surface Material & Paper Tooth Presets
 * =========================================================================================
 *
 * Defines the physical substrate that brushes and media interact with (paper tooth depth,
 * grain scale, absorbency, and dry-media friction drag).
 */

#include <cstdint>

namespace Folio {

/**
 * @enum SurfaceTexturePattern
 * @brief Procedural substrate micro-geometry pattern evaluated in world coordinates.
 */
enum class SurfaceTexturePattern : uint8_t {
    SmoothPaper      = 0, ///< Fine, minimal tooth (ink, marker, technical pen)
    ColdPressPaper   = 1, ///< Organic cellular dimples (watercolor, pencil, charcoal)
    RoughPastelTooth = 2, ///< High-contrast directional tooth (pastels, dry chalk)
    WovenCanvas      = 3, ///< Orthogonal linen weave grid (oil bristle, acrylic)
    ConcreteWall     = 4  ///< Coarse pitted surface (graffiti / aerosol spray)
};

/**
 * @struct CanvasMaterial
 * @brief Physical substrate properties modulating pigment pickup, bleed, and brush depletion.
 */
struct CanvasMaterial {
    SurfaceTexturePattern pattern      = SurfaceTexturePattern::SmoothPaper;
    float                 toothDepth   = 0.0f; ///< Surface peak-to-valley roughness [0.0 = glass, 1.0 = deep tooth]
    float                 toothScaleMm = 0.8f; ///< Physical grain wavelength in millimeters
    float                 absorbency   = 0.2f; ///< Liquid absorption [0.0 = non-porous, 1.0 = raw blotting paper]
    float                 surfaceDrag  = 0.1f; ///< Physical friction multiplier for dry-media skimming [0.0, 1.0]

    [[nodiscard]] static constexpr CanvasMaterial SmoothPaper() noexcept {
        return { SurfaceTexturePattern::SmoothPaper, 0.0f, 0.5f, 0.15f, 0.05f };
    }

    [[nodiscard]] static constexpr CanvasMaterial SketchbookFine() noexcept {
        return { SurfaceTexturePattern::ColdPressPaper, 0.25f, 0.6f, 0.3f, 0.15f };
    }

    [[nodiscard]] static constexpr CanvasMaterial ColdPressWatercolor() noexcept {
        return { SurfaceTexturePattern::ColdPressPaper, 0.55f, 1.2f, 0.8f, 0.25f };
    }

    [[nodiscard]] static constexpr CanvasMaterial RoughPastel() noexcept {
        return { SurfaceTexturePattern::RoughPastelTooth, 0.75f, 0.9f, 0.4f, 0.45f };
    }

    [[nodiscard]] static constexpr CanvasMaterial OilCanvasWeave() noexcept {
        return { SurfaceTexturePattern::WovenCanvas, 0.65f, 1.1f, 0.1f, 0.35f };
    }

    [[nodiscard]] static constexpr CanvasMaterial ConcreteWall() noexcept {
        return { SurfaceTexturePattern::ConcreteWall, 0.85f, 1.6f, 0.5f, 0.6f };
    }
};

} // namespace Folio
