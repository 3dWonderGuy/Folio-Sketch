#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/input_manager.hpp
 * @brief Unified Multi-Device Input Dispatcher & Palm Rejection State Machine
 * =========================================================================================
 *
 * Rules:
 *   1. PENCIL / STYLUS : Inking only (pressure, tilt, unbuffered 240Hz).
 *   2. TOUCH / FINGER  : Translation (Pan) and Pin-point navigation only. Never inks!
 *   3. MOUSE           : Inking only (simulates drawing on desktop).
 *
 * Hardware Palm Rejection: While Stylus is active, all finger touches are rejected.
 */

#include <cstdint>
#include <functional>
#include <unordered_map>
#include "folio_sketch/core/raw_input.hpp"
#include "folio_backends/common/gl_capsule_renderer.hpp"

namespace Folio::V2 {

enum class DeviceType : uint8_t {
    Stylus, ///< S-Pen, Apple Pencil, or active digitizer (Inking only)
    Touch,  ///< Finger touch (Translation & Navigation only)
    Mouse   ///< Desktop mouse (Left-drag Inking only)
};

enum class InputPhase : uint8_t {
    Down,
    Move,
    Up,
    Cancel
};

/**
 * @struct PointerPacket
 * @brief Normalized pointer sample received from the OS (Android MotionEvent or SDL3 event).
 */
struct PointerPacket {
    DeviceType device       = DeviceType::Stylus;
    InputPhase phase        = InputPhase::Down;
    int32_t    pointerId    = 0;
    float      x            = 0.0f; ///< Screen-space X (pixels)
    float      y            = 0.0f; ///< Screen-space Y (pixels)
    float      pressure     = 0.5f; ///< [0, 1] normalized stylus pressure
    float      tiltX        = 0.0f; ///< Stylus tilt X angle (radians)
    float      tiltY        = 0.0f; ///< Stylus tilt Y angle (radians)
    double     timestampSec = 0.0;
};

class InputManager {
public:
    // Callbacks for Inking (Stylus and Mouse only)
    std::function<void(const PointerPacket&)> onInkBegin;
    std::function<void(const PointerPacket&)> onInkMove;
    std::function<void(const PointerPacket&)> onInkEnd;

    // Callbacks for Navigation (Touch / Fingers only)
    std::function<void(float deltaX, float deltaY)> onCanvasTranslate;
    std::function<void(float focalX, float focalY, float zoomFactor)> onCanvasZoom;

    InputManager() = default;

    /**
     * @brief Ingests an incoming raw hardware pointer packet and routes it strictly
     *        according to the device rules and palm-rejection state.
     */
    void ProcessPacket(const PointerPacket& packet);

    /**
     * @brief Returns true if a stylus or mouse inking gesture is currently active.
     */
    [[nodiscard]] bool IsInkingActive() const noexcept { return m_inkingActive; }

    /**
     * @brief Returns true if finger touches are currently panning/translating the canvas.
     */
    [[nodiscard]] bool IsTranslatingActive() const noexcept { return m_translatingActive; }

    /**
     * @brief Cancels any active gestures (e.g. on window focus loss).
     */
    void Reset();

private:
    struct TouchPoint {
        float x = 0.0f;
        float y = 0.0f;
    };

    bool m_inkingActive             = false;
    bool m_translatingActive        = false;
    int32_t m_activeInkingPointerId = -1;
    DeviceType m_activeInkingDevice = DeviceType::Stylus;

    std::unordered_map<int32_t, TouchPoint> m_activeFingers;
    float m_prevPinchDist = 0.0f;
    float m_prevMidX      = 0.0f;
    float m_prevMidY      = 0.0f;

    void HandleStylusOrMouse(const PointerPacket& packet);
    void HandleTouch(const PointerPacket& packet);
};

} // namespace Folio::V2
