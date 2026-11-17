/**
 * =========================================================================================
 * @file folio_backends/ios/ios_presenter.cpp
 * @brief Apple iOS / iPadOS Low-Latency Surface Presenter Implementation
 * =========================================================================================
 */

#include "folio_backends/ios/ios_presenter.hpp"

#if defined(__APPLE__)

namespace Folio::Backends {

IosPresenter::~IosPresenter() {
    Shutdown();
}

bool IosPresenter::Init(void* metalLayerOrView) {
    Shutdown();
    m_nativeHandle = metalLayerOrView;
    m_initialized = true;
    return true;
}

void IosPresenter::Shutdown() {
    m_nativeHandle = nullptr;
    m_width = 0;
    m_height = 0;
    m_initialized = false;
}

bool IosPresenter::BeginWetDraw(int* outWidth, int* outHeight) {
    if (!m_initialized) return false;
    if (outWidth) *outWidth = m_width;
    if (outHeight) *outHeight = m_height;
    return true;
}

void IosPresenter::EndWetDraw() const {
    // Metal CAMetalLayer drawable commit / transaction present hook will be invoked here.
}

void IosPresenter::Clear(float r, float g, float b, float a) {
    (void)r;
    (void)g;
    (void)b;
    (void)a;
}

void RequestFrontBufferFlush() {}

void RequestAndroidFrontBufferFlush() {}

std::unique_ptr<ISurfacePresenter> CreatePlatformPresenter() {
    return std::make_unique<IosPresenter>();
}

} // namespace Folio::Backends

#endif // __APPLE__
