cmake_minimum_required(VERSION 3.20)
project(FolioBackends VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FOLIO_BACKENDS_COMMON_SOURCES
    common/gl_capsule_renderer.cpp
    common/gl_raster_dab_renderer.cpp
    common/display_surface_transform.cpp
)

set(FOLIO_BACKENDS_COMMON_HEADERS
    common/gl_capsule_renderer.hpp
    common/gl_raster_dab_renderer.hpp
    common/display_surface_transform.hpp
    common/surface_presenter.hpp
)

if(ANDROID)
    set(FOLIO_BACKENDS_PLATFORM_SOURCES
        android/android_frontbuffer.cpp
        android/folio_android_jni.cpp
    )
    set(FOLIO_BACKENDS_PLATFORM_HEADERS
        android/android_frontbuffer.hpp
    )
elseif(APPLE)
    set(FOLIO_BACKENDS_PLATFORM_SOURCES
        ios/ios_presenter.cpp
    )
    set(FOLIO_BACKENDS_PLATFORM_HEADERS
        ios/ios_presenter.hpp
    )
else()
    # Shared Desktop (Linux & Windows)
    set(FOLIO_BACKENDS_PLATFORM_SOURCES
        desktop/desktop_presenter.cpp
    )
    set(FOLIO_BACKENDS_PLATFORM_HEADERS
        desktop/desktop_presenter.hpp
    )
endif()

add_library(folio_backends STATIC
    ${FOLIO_BACKENDS_COMMON_SOURCES}
    ${FOLIO_BACKENDS_COMMON_HEADERS}
    ${FOLIO_BACKENDS_PLATFORM_SOURCES}
    ${FOLIO_BACKENDS_PLATFORM_HEADERS}
)

add_library(Folio::Backends ALIAS folio_backends)

target_include_directories(folio_backends
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inspectorV2.0>
        $<INSTALL_INTERFACE:src>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../inspectorV2.0"
)

target_link_libraries(folio_backends
    PUBLIC
        folio_sketch
)

if(TARGET SDL3::SDL3)
    target_link_libraries(folio_backends PRIVATE SDL3::SDL3)
endif()

if(TARGET imgui)
    target_link_libraries(folio_backends PRIVATE imgui)
endif()

if(ANDROID)
    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(ANDROID_GLES3_LIB GLESv3)
    find_library(ANDROID_EGL_LIB EGL)
    target_link_libraries(folio_backends PRIVATE
        ${ANDROID_LOG_LIB}
        ${ANDROID_LIB}
        ${ANDROID_GLES3_LIB}
        ${ANDROID_EGL_LIB}
    )
elseif(IOS)
    # iOS uses OpenGLES.framework (or Metal), standard desktop OpenGL is not available on iOS SDK
    find_library(OPENGLES_LIB OpenGLES)
    if(OPENGLES_LIB)
        target_link_libraries(folio_backends PRIVATE ${OPENGLES_LIB})
    endif()
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(folio_backends PRIVATE OpenGL::GL)
endif()
