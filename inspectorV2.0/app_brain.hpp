#pragma once
/**
 * =========================================================================================
 * @file inspectorV2.0/app_brain.hpp
 * @brief Singleton Application Brain connecting SDL3 Host with Zero-Latency Scanout Engine
 * =========================================================================================
 */

#include <atomic>
#include "inspectorV2.0/layer_manager.hpp"

namespace Folio::V2 {

class AppBrain {
public:
    static AppBrain& Instance() {
        static AppBrain s_instance;
        return s_instance;
    }

    LayerManager& GetLayerManager() noexcept { return m_layerManager; }
    const LayerManager& GetLayerManager() const noexcept { return m_layerManager; }

    void RequestRedraw() noexcept {
        m_needsRedraw = true;
    }

    bool CheckAndResetRedraw() noexcept {
        bool expected = true;
        return m_needsRedraw.exchange(false);
    }

private:
    AppBrain() = default;
    ~AppBrain() = default;

    AppBrain(const AppBrain&) = delete;
    AppBrain& operator=(const AppBrain&) = delete;

    LayerManager m_layerManager;
    std::atomic<bool> m_needsRedraw{true};
};

} // namespace Folio::V2
