cmake_minimum_required(VERSION 3.28)
# VERSION is read by packaging/build-installer.ps1.
project(transparent VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# vision.cpp is consumed as a static library (BUILD_SHARED_LIBS=OFF below).
# It is vendored as a pinned git submodule (external/vision.cpp) instead of
# FetchContent, so builds are reproducible and release CI needs no network
# access to the project's Forgejo. It is the project's own fork (see PLAN.md,
# "Denoising"), which adds the SCUNet denoising architecture and folds in the
# VISP_STATIC_DEFINE fix (formerly applied by cmake/patch-visioncpp-static.cmake,
# now deleted): the headers mark the public API with VISP_API, which is
# __declspec(dllimport) on MSVC unless VISP_STATIC_DEFINE is defined. Without
# it the link fails with LNK2019 on "__imp_..." symbols.
#
# Consumers of visioncpp (transparent_core, tests) need VISP_STATIC_DEFINE so
# VISP_API expands to nothing instead of dllimport. This propagates into the
# visioncpp subdirectory too, where it is also correct: the static library
# then exports nothing via dllexport.
add_compile_definitions(VISP_STATIC_DEFINE)

option(TRANSPARENT_BUILD_TESTS "Build unit tests" ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(VISP_VULKAN ON CACHE BOOL "" FORCE)
set(VISP_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/vision.cpp)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network Test)
qt_standard_project_setup()

find_package(GIF REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory(models)
add_subdirectory(src)

if(TRANSPARENT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
