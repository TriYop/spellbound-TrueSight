function(add_truesight_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${ARGN})
  target_include_directories(${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/Source)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_truesight_test(test_band_config test_band_config.cpp AudioPluginsCommon::analysis)
