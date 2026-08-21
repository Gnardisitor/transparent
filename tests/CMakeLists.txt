find_package(Qt6 REQUIRED COMPONENTS Test)

function(add_transparent_test test_name)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE transparent_core Qt6::Test)
  target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_transparent_test(test_pipeline)
add_transparent_test(test_background_removal_step)
add_transparent_test(test_batch_runner)
