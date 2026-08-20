cmake_minimum_required(VERSION 3.28)
project(transparent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(TRANSPARENT_BUILD_TESTS "Build unit tests" ON)

# vision.cpp (ggml-based inference, MIT) — chosen over ncnn because its
# conversion/op coverage handles the transformer + deformable-conv ops
# BiRefNet needs; ncnn's pnnx/onnx2ncnn tooling cannot translate them (see
# PLAN.md, "Inference engine"). Vendored via FetchContent rather than
# vcpkg (no vcpkg port exists) and pinned to a tagged release for
# reproducibility.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(VISP_VULKAN ON CACHE BOOL "" FORCE)
set(VISP_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(visioncpp
  GIT_REPOSITORY https://github.com/Acly/vision.cpp.git
  GIT_TAG v0.3.1
  GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(visioncpp)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
qt_standard_project_setup()

# Scoped to our own subdirectories, added after vision.cpp so its (non-Qt)
# targets don't pick these up and warn about missing Qt AUTOGEN support.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory(models)
add_subdirectory(src)

if(TRANSPARENT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
