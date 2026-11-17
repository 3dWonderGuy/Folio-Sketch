/**
 * =========================================================================================
 * @file inspectorV2.0/input_manager.cpp
 * @brief Implementation of Unified Multi-Device Input Dispatcher & Palm Rejection
 * =========================================================================================
 */

#include "inspectorV2.0/input_manager.hpp"
#include <cmath>
#include <algorithm>

namespace Folio::V2 {

void InputManager::ProcessPacket(const PointerPacket& packet) {
    switch (packet.device) {
        case DeviceType::Stylus:
        case DeviceType::Mouse:
            HandleStylusOrMouse(packet);
            break;

        case DeviceType::Touch:
            HandleTouch(packet);
            break;
    }
}

void InputManager::HandleStylusOrMouse(const PointerPacket& packet) {
    switch (packet.phase) {
        case InputPhase::Down: {
            // Palm rejection: kill any ongoing finger translation when pencil lands
            m_translatingActive = false;
            m_activeFingers.clear();

            m_inkingActive          = true;
            m_activeInkingPointerId = packet.pointerId;
            m_activeInkingDevice    = packet.device;

            if (onInkBegin) {
                onInkBegin(packet);
            }
            break;
        }

        case InputPhase::Move: {
            if (m_inkingActive && packet.pointerId == m_activeInkingPointerId) {
                if (onInkMove) {
                    onInkMove(packet);
                }
            }
            break;
        }

        case InputPhase::Up:
        case InputPhase::Cancel: {
            if (m_inkingActive && packet.pointerId == m_activeInkingPointerId) {
                if (onInkEnd) {
                    onInkEnd(packet);
                }
                m_inkingActive = false;
                m_activeInkingPointerId = -1;
            }
            break;
        }
    }
}

void InputManager::HandleTouch(const PointerPacket& packet) {
    // 1. HARDWARE PALM REJECTION:
    // If the pencil or mouse is currently drawing, ignore all touches (resting palm)
    if (m_inkingActive) {
        return;
    }

    switch (packet.phase) {
        case InputPhase::Down: {
            m_activeFingers[packet.pointerId] = TouchPoint{ packet.x, packet.y };

            // When exactly 2 fingers are touching, initialize pinch/pan baseline
            if (m_activeFingers.size() == 2) {
                auto it = m_activeFingers.begin();
                const auto& p1 = it->second;
                ++it;
                const auto& p2 = it->second;

                m_prevMidX = (p1.x + p2.x) * 0.5f;
                m_prevMidY = (p1.y + p2.y) * 0.5f;
                m_prevPinchDist = std::hypot(p2.x - p1.x, p2.y - p1.y);
                m_translatingActive = true;
            }
            break;
        }

        case InputPhase::Move: {
            auto it = m_activeFingers.find(packet.pointerId);
            if (it == m_activeFingers.end()) {
                return;
            }

            it->second.x = packet.x;
            it->second.y = packet.y;

            // 2. TWO-FINGER PINCH-TO-ZOOM & PAN (FolioNote TouchGestureRecognizer model)
            // Single finger (resting palm) NEVER pans or zooms the canvas.
            if (m_activeFingers.size() == 2) {
                auto fit = m_activeFingers.begin();
                const auto& p1 = fit->second;
                ++fit;
                const auto& p2 = fit->second;

                const float midX = (p1.x + p2.x) * 0.5f;
                const float midY = (p1.y + p2.y) * 0.5f;
                const float dist = std::hypot(p2.x - p1.x, p2.y - p1.y);

                if (m_prevPinchDist > 1.0f) {
                    const float dx = midX - m_prevMidX;
                    const float dy = midY - m_prevMidY;
                    const float zoomDelta = dist / m_prevPinchDist;

                    if (onCanvasTranslate && (std::abs(dx) > 0.0f || std::abs(dy) > 0.0f)) {
                        onCanvasTranslate(dx, dy);
                    }

                    if (onCanvasZoom && std::abs(zoomDelta - 1.0f) > 0.001f) {
                        onCanvasZoom(midX, midY, zoomDelta);
                    }
                }

                m_prevMidX = midX;
                m_prevMidY = midY;
                m_prevPinchDist = dist;
                m_translatingActive = true;
            }
            break;
        }

        case InputPhase::Up:
        case InputPhase::Cancel: {
            m_activeFingers.erase(packet.pointerId);
            if (m_activeFingers.size() < 2) {
                m_prevPinchDist = 0.0f;
                m_prevMidX = 0.0f;
                m_prevMidY = 0.0f;
            }
            if (m_activeFingers.empty()) {
                m_translatingActive = false;
            }
            break;
        }
    }
}

void InputManager::Reset() {
    if (m_inkingActive) {
        if (onInkEnd) {
            PointerPacket cancelPkt;
            cancelPkt.device    = m_activeInkingDevice;
            cancelPkt.phase     = InputPhase::Cancel;
            cancelPkt.pointerId = m_activeInkingPointerId;
            onInkEnd(cancelPkt);
        }
    }
    m_inkingActive          = false;
    m_translatingActive     = false;
    m_activeInkingPointerId = -1;
    m_prevPinchDist         = 0.0f;
    m_prevMidX              = 0.0f;
    m_prevMidY              = 0.0f;
    m_activeFingers.clear();
}

} // namespace Folio::V2
