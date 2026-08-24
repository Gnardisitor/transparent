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
  core/ModelCatalog.cpp
  core/ModelManager.cpp
)
target_include_directories(transparent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Qt6::Network is here (not just on the executable) because ModelManager,
# which downloads curated models on demand, lives in this library.
target_link_libraries(transparent_core PUBLIC Qt6::Gui Qt6::Network visioncpp GIF::GIF)

add_executable(transparent
  main.cpp
  ui/MainWindow.cpp
  ui/SettingsPage.cpp
  ui/SpinnerWidget.cpp
  ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)
target_link_libraries(transparent PRIVATE transparent_core Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(transparent PRIVATE
  TRANSPARENT_MODELS_DIR="${CMAKE_BINARY_DIR}/models"
)

# AppImage packaging (packaging/build-appimage.sh) installs into an AppDir
# using these same rules, following freedesktop's hicolor icon theme
# convention so linuxdeploy picks the icon up automatically.
install(TARGETS transparent RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/transparent.desktop
  DESTINATION share/applications)
install(FILES ${CMAKE_SOURCE_DIR}/resources/icon.png
  DESTINATION share/icons/hicolor/256x256/apps
  RENAME transparent.png)
