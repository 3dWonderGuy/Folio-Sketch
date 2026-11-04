cmake_minimum_required(VERSION 3.20)
project(FolioSketch VERSION 1.0.0 LANGUAGES CXX C)

# C++20 Standard Requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    set(CMAKE_OBJECT_PATH_MAX 1024)
    add_compile_options(/FS /bigobj /utf-8 /MP)
    add_compile_definitions(
        NOMINMAX
        _USE_MATH_DEFINES
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(FOLIO_SKETCH_STANDALONE ON)
else()
    set(FOLIO_SKETCH_STANDALONE OFF)
endif()

option(FOLIO_SKETCH_BUILD_INSPECTOR "Build interactive Inking Diagnostic Playground and Inspector" ${FOLIO_SKETCH_STANDALONE})

# ==============================================================================
# InkStrokeModeler Dependency Resolution
# ==============================================================================
set(INK_STROKE_MODELER_DIR "")
if(NOT TARGET InkStrokeModeler::stroke_modeler)
    find_package(InkStrokeModeler QUIET)
    if(NOT InkStrokeModeler_FOUND)
        # 1. Check local third_party/ink-stroke-modeler
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ink-stroke-modeler/CMakeLists.txt")
            set(INK_STROKE_MODELER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ink-stroke-modeler")
            set(INK_STROKE_MODELER_BUILD_TESTING OFF CACHE BOOL "" FORCE)
            set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
            set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
            set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/abseil-cpp")
                set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/abseil-cpp" CACHE PATH "" FORCE)
                set(FETCHCONTENT_SOURCE_DIR_ABSEIL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/abseil-cpp" CACHE PATH "" FORCE)
            elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp")
                set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp" CACHE PATH "" FORCE)
                set(FETCHCONTENT_SOURCE_DIR_ABSEIL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp" CACHE PATH "" FORCE)
            endif()
            add_subdirectory(third_party/ink-stroke-modeler ink_sm EXCLUDE_FROM_ALL)
        # 2. Check sibling FolioNote repository
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/ink-stroke-modeler/CMakeLists.txt")
            set(INK_STROKE_MODELER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/ink-stroke-modeler")
            set(INK_STROKE_MODELER_BUILD_TESTING OFF CACHE BOOL "" FORCE)
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/abseil-cpp")
                set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/abseil-cpp" CACHE PATH "" FORCE)
                set(FETCHCONTENT_SOURCE_DIR_ABSEIL_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/abseil-cpp" CACHE PATH "" FORCE)
            endif()
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/ink-stroke-modeler" ink_sm EXCLUDE_FROM_ALL)
        else()
            include(FetchContent)
            FetchContent_Declare(
                ink_stroke_modeler
                GIT_REPOSITORY https://github.com/google/ink-stroke-modeler.git
                GIT_TAG main
            )
            set(INK_STROKE_MODELER_BUILD_TESTING OFF CACHE BOOL "" FORCE)
            FetchContent_MakeAvailable(ink_stroke_modeler)
            set(INK_STROKE_MODELER_DIR "${ink_stroke_modeler_SOURCE_DIR}")
        endif()
    endif()
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ink-stroke-modeler")
        set(INK_STROKE_MODELER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ink-stroke-modeler")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ink-stroke-modeler")
        set(INK_STROKE_MODELER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ink-stroke-modeler")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/ink-stroke-modeler")
        set(INK_STROKE_MODELER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/ink-stroke-modeler")
    endif()
endif()

# ==============================================================================
# FolioSketch Library Target
# ==============================================================================
set(FOLIO_SKETCH_SOURCES
    # Core Engine
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ink_engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ink_physics_bridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ink_stroke_finisher.cpp"

    # Geometry & Filtering
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_stroke_filter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_geometry_utils.cpp"

    # Vector Geometry & Ribbon Generators
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_polygon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_stroke.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_ribbon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_caps.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_patterns.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_infill.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_ribbon_corner.cpp"

    # Diagnostics & Debug
    "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/ink_generator_debug.cpp"

    # GPU Mesh Builder
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/ink_mesh_builder.cpp"
)

set(FOLIO_SKETCH_HEADERS
    # Umbrella Header
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/folio_sketch.hpp"

    # Core Foundations & Data Types
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/core/ink_stroke_data.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/core/ink_config.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/core/stroke_collision.hpp"

    # Engine
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/engine/ink_engine.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/engine/stroke_width_evaluator.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/engine/stroke_post_processor.hpp"

    # GPU
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/gpu/ink_mesh.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/gpu/gl_capsule_renderer.hpp"

    # Geometry
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/geometry/shape_recognizer.hpp"

    # Vector
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/vector/ink_path.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/vector/ink_generator.hpp"

    # Internal Headers
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ink_physics_bridge.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ink_stroke_finisher.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_geometry_utils.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_nib_geometry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_stroke_filter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_generator_internal.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vector/ink_ribbon_corner.hpp"
)

add_library(FolioSketch STATIC
    ${FOLIO_SKETCH_SOURCES}
    ${FOLIO_SKETCH_HEADERS}
)

add_library(FolioSketch::FolioSketch ALIAS FolioSketch)
add_library(Folio::Sketch ALIAS FolioSketch)
add_library(folio_sketch ALIAS FolioSketch)

target_include_directories(FolioSketch
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${INK_STROKE_MODELER_DIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/generator"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh"
)

target_link_libraries(FolioSketch
    PUBLIC
        InkStrokeModeler::stroke_modeler
)

# Compiler warnings and optimization flags
if(MSVC)
    target_compile_options(FolioSketch PRIVATE /W4 /permissive-)
else()
    # Suppress pedantic / non-standard extension warnings in third-party headers (e.g. abseil __int128)
    target_compile_options(FolioSketch PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-pedantic>
    )
endif()

# ==============================================================================
# Interactive Inking Diagnostic Playground & Inspector (folio_ink_inspector)
# Standalone demo + test app. SDL3 and Dear ImGui are vendored in third_party/.
# Defined BEFORE the test suite so it is the primary/default runnable target.
# ==============================================================================
if(FOLIO_SKETCH_BUILD_INSPECTOR)
    # --- Vendored SDL3 (static) ---
    if(NOT TARGET SDL3::SDL3)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
        set(SDL_IBUS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL" sdl EXCLUDE_FROM_ALL)
        if(NOT TARGET SDL3::SDL3 AND TARGET SDL3-static)
            add_library(SDL3::SDL3 ALIAS SDL3-static)
        endif()
    endif()

    # --- Vendored Dear ImGui (SDL3 + OpenGL3 backends) ---
    set(FOLIO_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    add_library(imgui STATIC
        "${FOLIO_IMGUI_DIR}/imgui.cpp"
        "${FOLIO_IMGUI_DIR}/imgui_draw.cpp"
        "${FOLIO_IMGUI_DIR}/imgui_tables.cpp"
        "${FOLIO_IMGUI_DIR}/imgui_widgets.cpp"
        "${FOLIO_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        "${FOLIO_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    target_include_directories(imgui PUBLIC "${FOLIO_IMGUI_DIR}" "${FOLIO_IMGUI_DIR}/backends")
    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
    if(ANDROID)
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
    endif()

    # --- OpenGL / GLES ---
    if(NOT ANDROID)
        find_package(OpenGL REQUIRED)
    endif()

    set(FOLIO_INSPECTOR_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/main_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gl_capsule_renderer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/raw_sensor_layer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/modeler_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/width_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/geometry_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/stroke_scrubber.cpp"
    )

    if(ANDROID)
        # Android SDLActivity loads libmain.so
        add_library(main SHARED ${FOLIO_INSPECTOR_SOURCES})
        set_target_properties(main PROPERTIES
            C_VISIBILITY_PRESET default
            CXX_VISIBILITY_PRESET default
            VISIBILITY_INLINES_HIDDEN 0
        )
        set(INSPECTOR_TARGET main)
    else()
        add_executable(folio_ink_inspector ${FOLIO_INSPECTOR_SOURCES})
        set(INSPECTOR_TARGET folio_ink_inspector)
    endif()

    target_include_directories(${INSPECTOR_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(${INSPECTOR_TARGET} PRIVATE folio_sketch SDL3::SDL3 imgui)
    if(ANDROID)
        find_library(ANDROID_LOG_LIB log)
        find_library(ANDROID_LIB android)
        find_library(ANDROID_GLES3_LIB GLESv3)
        find_library(ANDROID_EGL_LIB EGL)
        target_compile_definitions(${INSPECTOR_TARGET} PRIVATE IMGUI_IMPL_OPENGL_ES3)
        target_link_libraries(${INSPECTOR_TARGET} PRIVATE
            ${ANDROID_LOG_LIB}
            ${ANDROID_LIB}
            ${ANDROID_GLES3_LIB}
            ${ANDROID_EGL_LIB}
        )
    else()
        target_link_libraries(${INSPECTOR_TARGET} PRIVATE OpenGL::GL)
        if(WIN32)
            target_link_libraries(${INSPECTOR_TARGET} PRIVATE dwmapi ole32 oleaut32 shell32 winspool)
        endif()
    endif()

    if(NOT ANDROID)
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT folio_ink_inspector)
    endif()
endif()
