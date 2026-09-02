find_package(Qt6 REQUIRED COMPONENTS Test)

function(add_transparent_test test_name)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE transparent_core Qt6::Test)
  target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_transparent_test(test_pipeline)
add_transparent_test(test_background_removal_step)
add_transparent_test(test_upscale_step)
add_transparent_test(test_denoise_step)
add_transparent_test(test_batch_runner)
add_transparent_test(test_bokeh_step)
add_transparent_test(test_gif_io)
add_transparent_test(test_model_catalog)
add_transparent_test(test_model_manager)

add_executable(test_main_window test_main_window.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/SettingsPage.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/SpinnerWidget.cpp
)
target_link_libraries(test_main_window PRIVATE transparent_core Qt6::Widgets Qt6::Concurrent Qt6::Test)
target_include_directories(test_main_window PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_main_window COMMAND test_main_window)
set_tests_properties(test_main_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
