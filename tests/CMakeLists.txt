find_package(Qt6 REQUIRED COMPONENTS Test)

# Test executables live in build/tests, not next to transparent.exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# Test binaries link Qt DLLs that live only in the Qt install, so prepend its
# bin dir to each test process's PATH. Without this, ctest from a plain shell
# fails with a missing Qt6Test.dll loader error. Qt6_DIR sits three levels
# below the Qt prefix.
get_filename_component(QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)

function(add_transparent_test test_name)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE transparent_core Qt6::Test)
  target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_PREFIX}/bin")
endfunction()

add_transparent_test(test_pipeline)
add_transparent_test(test_background_removal_step)
add_transparent_test(test_upscale_step)
add_transparent_test(test_denoise_step)
add_transparent_test(test_batch_runner)
add_transparent_test(test_bokeh_step)
add_transparent_test(test_gif_io)
add_transparent_test(test_app_identity)
add_transparent_test(test_model_catalog)
add_transparent_test(test_model_manager)

add_executable(test_main_window test_main_window.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/SettingsPage.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/SpinnerWidget.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/PreviewCanvas.cpp
)
target_link_libraries(test_main_window PRIVATE transparent_core Qt6::Widgets Qt6::Concurrent Qt6::Test)
target_include_directories(test_main_window PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_main_window COMMAND test_main_window)
set_tests_properties(test_main_window PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_PREFIX}/bin")

add_executable(test_preview_canvas test_preview_canvas.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/PreviewCanvas.cpp
)
target_link_libraries(test_preview_canvas PRIVATE Qt6::Widgets Qt6::Test)
target_include_directories(test_preview_canvas PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_preview_canvas COMMAND test_preview_canvas)
set_tests_properties(test_preview_canvas PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_PREFIX}/bin")
