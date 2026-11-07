cmake_minimum_required(VERSION 3.20)
project(FolioBackends VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FOLIO_BACKENDS_SOURCES
    common/gl_capsule_renderer.cpp
    android/android_frontbuffer.cpp
)

set(FOLIO_BACKENDS_HEADERS
    common/gl_capsule_renderer.hpp
    android/android_frontbuffer.hpp
)

add_library(folio_backends STATIC
    ${FOLIO_BACKENDS_SOURCES}
    ${FOLIO_BACKENDS_HEADERS}
)

add_library(Folio::Backends ALIAS folio_backends)

target_include_directories(folio_backends
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../inspector"
)

target_link_libraries(folio_backends
    PUBLIC
        folio_sketch
        imgui
)

if(TARGET SDL3::SDL3)
    target_link_libraries(folio_backends PRIVATE SDL3::SDL3)
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
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(folio_backends PRIVATE OpenGL::GL)
endif()
