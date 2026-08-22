# Core logic (image pipeline, vision.cpp inference) as a library, separate
# from the executable, so tests can link against it without pulling in Qt
# Widgets or main().
add_library(transparent_core STATIC
  core/Pipeline.cpp
  core/VisionCppSegmentationModel.cpp
  core/BackgroundRemovalStep.cpp
  core/VisionCppUpscaleModel.cpp
  core/UpscaleStep.cpp
  core/BokehStep.cpp
  core/BatchRunner.cpp
  core/ImageFormats.cpp
  core/GifIO.cpp
)
target_include_directories(transparent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(transparent_core PUBLIC Qt6::Gui visioncpp GIF::GIF)

add_executable(transparent
  main.cpp
  ui/MainWindow.cpp
  ui/SpinnerWidget.cpp
)
target_link_libraries(transparent PRIVATE transparent_core Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(transparent PRIVATE
  TRANSPARENT_MODELS_DIR="${CMAKE_BINARY_DIR}/models"
)
