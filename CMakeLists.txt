cmake_minimum_required(VERSION 3.20)
project(FolioSketch VERSION 1.0.0 LANGUAGES CXX C)

# C++20 Standard Requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to build test suite (default ON if top-level, OFF if subproject)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(FOLIO_SKETCH_STANDALONE ON)
else()
    set(FOLIO_SKETCH_STANDALONE OFF)
endif()

option(FOLIO_SKETCH_BUILD_TESTS "Build Folio-Sketch standalone test suite and benchmarks" OFF)
option(FOLIO_SKETCH_BUILD_INSPECTOR "Build interactive Inking Diagnostic Playground and Inspector" ${FOLIO_SKETCH_STANDALONE})
option(FOLIO_SKETCH_ENABLE_THORSVG "Enable ThorVG rasterizer and vector bridge (needed by tests)" ${FOLIO_SKETCH_BUILD_TESTS})

# ==============================================================================
# ThorVG Dependency Resolution (Only when ThorVG is enabled)
# ==============================================================================
if(FOLIO_SKETCH_ENABLE_THORSVG)
    if(NOT TARGET thorvg::thorvg AND NOT TARGET thorvg)
        find_package(thorvg QUIET)
        if(NOT thorvg_FOUND)
            set(THORVG_SRC_DIR "")
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thorvg/meson.build")
                set(THORVG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thorvg")
            elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/thorvg/meson.build")
                set(THORVG_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FolioNote/third_party/thorvg")
            else()
                include(FetchContent)
                FetchContent_Declare(
                    thorvg_fetch
                    GIT_REPOSITORY https://github.com/thorvg/thorvg.git
                    GIT_TAG main
                    GIT_SHALLOW TRUE
                )
                FetchContent_MakeAvailable(thorvg_fetch)
                set(THORVG_SRC_DIR "${thorvg_fetch_SOURCE_DIR}")
            endif()

            include(ExternalProject)
            set(THORVG_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/thorvg_build")
            set(THORVG_STATIC_LIB "${THORVG_BUILD_DIR}/src/libthorvg-1.a")

            ExternalProject_Add(
                thorvg_project
                SOURCE_DIR "${THORVG_SRC_DIR}"
                BINARY_DIR "${THORVG_BUILD_DIR}"
                CONFIGURE_COMMAND meson setup "${THORVG_SRC_DIR}" "${THORVG_BUILD_DIR}"
                                  --default-library=static
                                  -Dloaders=svg
                                  -Dsavers=
                                  -Dbindings=
                                  --buildtype=release
                BUILD_COMMAND ninja -C "${THORVG_BUILD_DIR}"
                INSTALL_COMMAND ""
                BUILD_BYPRODUCTS "${THORVG_STATIC_LIB}"
            )

            add_library(thorvg::thorvg STATIC IMPORTED GLOBAL)
            add_dependencies(thorvg::thorvg thorvg_project)

            file(MAKE_DIRECTORY "${THORVG_BUILD_DIR}/src")

            find_package(OpenMP QUIET)
            set(THORVG_EXTRA_LIBS "pthread")
            if(TARGET OpenMP::OpenMP_CXX)
                list(APPEND THORVG_EXTRA_LIBS OpenMP::OpenMP_CXX)
            endif()

            set_target_properties(thorvg::thorvg PROPERTIES
                IMPORTED_LOCATION "${THORVG_STATIC_LIB}"
                INTERFACE_INCLUDE_DIRECTORIES "${THORVG_SRC_DIR}/inc"
                INTERFACE_LINK_LIBRARIES "${THORVG_EXTRA_LIBS}"
            )
            add_library(thorvg ALIAS thorvg::thorvg)
        endif()
    endif()
endif()

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

    # Geometry & Filtering
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_stroke_filter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_geometry_utils.cpp"

    # Vector Geometry & Ribbon Generators
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_polygon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_stroke.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_ribbon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_caps.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_patterns.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_infill.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_debug.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_ribbon_corner.cpp"

    # GPU Mesh Builder
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_builder.cpp"
)

set(FOLIO_SKETCH_HEADERS
    # Public API Headers
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/folio_sketch.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/ink_path.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/ink_engine.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/ink_config.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/ink_stroke_data.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/ink_mesh.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/stroke_collision.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/stroke_width_evaluator.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/stroke_post_processor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/folio_sketch/ink_generator.hpp"

    # Internal Headers
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_geometry_utils.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_nib_geometry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/geometry/ink_stroke_filter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_generator_internal.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generator/ink_ribbon_corner.hpp"
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

if(FOLIO_SKETCH_ENABLE_THORSVG)
    target_link_libraries(FolioSketch PRIVATE thorvg::thorvg)
    target_compile_definitions(FolioSketch PRIVATE FOLIO_ENABLE_THORSVG=1)
endif()

# Compiler warnings and optimization flags
if(MSVC)
    target_compile_options(FolioSketch PRIVATE /W4 /permissive-)
else()
    target_compile_options(FolioSketch PRIVATE -Wall -Wextra -Wpedantic)
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

    add_executable(folio_ink_inspector
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/main_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/raw_sensor_layer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/modeler_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/width_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/geometry_inspector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/stroke_scrubber.cpp"
    )
    target_include_directories(folio_ink_inspector PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(folio_ink_inspector PRIVATE folio_sketch SDL3::SDL3 imgui)
    if(ANDROID)
        find_library(ANDROID_GLES3_LIB GLESv3)
        find_library(ANDROID_EGL_LIB EGL)
        target_link_libraries(folio_ink_inspector PRIVATE ${ANDROID_GLES3_LIB} ${ANDROID_EGL_LIB})
    else()
        target_link_libraries(folio_ink_inspector PRIVATE OpenGL::GL)
    endif()

    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT folio_ink_inspector)
endif()

# ==============================================================================
# Standalone Test Suite & Benchmarks (opt-in: -DFOLIO_SKETCH_BUILD_TESTS=ON)
# ==============================================================================
if(FOLIO_SKETCH_BUILD_TESTS)
    enable_testing()

    add_executable(test_ink_generator
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_ink_generator.cpp"
    )
    target_link_libraries(test_ink_generator PRIVATE FolioSketch::FolioSketch thorvg::thorvg)
    if(WIN32)
        target_link_libraries(test_ink_generator PRIVATE dwmapi ole32 shell32 winspool)
    endif()
    add_test(NAME test_ink_generator COMMAND test_ink_generator)
endif()
