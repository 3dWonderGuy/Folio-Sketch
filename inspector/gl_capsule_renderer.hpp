#pragma once
/**
 * =========================================================================================
 * @file src/debug/gl_capsule_renderer.hpp
 * @brief Forwarding bridge to folio_backends/common/gl_capsule_renderer.hpp
 * =========================================================================================
 */

#include "folio_backends/common/gl_capsule_renderer.hpp"

namespace Folio {
    using GlCapsuleRenderer = Folio::Backends::GlCapsuleRenderer;
    using CapsuleBrush      = Folio::Backends::CapsuleBrush;
    using CanvasTransform   = Folio::Backends::CanvasTransform;

    namespace Debug {
        using GlCapsuleRenderer = Folio::Backends::GlCapsuleRenderer;
        using CapsuleBrush      = Folio::Backends::CapsuleBrush;
        using CanvasTransform   = Folio::Backends::CanvasTransform;
    }
}
