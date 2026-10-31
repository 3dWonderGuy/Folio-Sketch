cmake_minimum_required(VERSION 3.20)
project(FolioSketch VERSION 1.0.0 LANGUAGES CXX)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_caps.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_corner_pivot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_round_ribbon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_polygonal_ribbon.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_patterns.cpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/ink_mesh_internal.hpp"
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
    target_compile_options(FolioSketch PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-pedantic>
    )
endif()
