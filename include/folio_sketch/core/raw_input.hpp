#pragma once
/**
 * =========================================================================================
 * @file folio_sketch/core/raw_input.hpp
 * @brief Backend-agnostic raw hardware digitizer packet (the core engine's only input type)
 * =========================================================================================
 *
 * Platform glue (Android MotionEvent, SDL pen events, Win32 WM_POINTER, evdev, ...) converts
 * whatever the OS delivers into RawInputSample and hands the batch to InkEngine::IngestSamples.
 * The core library never touches an OS or graphics header.
 */

#include <cstdint>

namespace Folio {

/**
 * @struct RawInputSample
 * @brief One digitizer packet in canvas millimetres with a monotonic nanosecond timestamp.
 *
 * Tilt is expressed as the stylus' projected inclination along the X and Y axes, in radians.
 * (0, 0) means perfectly upright. Pressure is [0, 1], or a negative value when the device
 * reports no pressure (mouse / finger).
 */
struct RawInputSample {
    double  x            = 0.0;   ///< Canvas X (mm)
    double  y            = 0.0;   ///< Canvas Y (mm)
    float   pressure     = 1.0f;  ///< Contact pressure [0, 1]; < 0 when unavailable
    float   tiltX        = 0.0f;  ///< Stylus tilt along X (radians)
    float   tiltY        = 0.0f;  ///< Stylus tilt along Y (radians)
    int64_t timeNs       = 0;     ///< Monotonic hardware timestamp (nanoseconds)

    // Compatibility fields for inspector telemetry
    float   tiltRad      = 0.0f;
    double  timestampSec = 0.0;

    [[nodiscard]] constexpr double TimeSec() const noexcept {
        if (timeNs > 0) return static_cast<double>(timeNs) * 1e-9;
        return timestampSec;
    }
};

} // namespace Folio
