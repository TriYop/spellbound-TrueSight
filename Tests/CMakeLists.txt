function(add_truesight_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${ARGN})
  target_include_directories(${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/Source)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_truesight_test(test_band_config test_band_config.cpp AudioPluginsCommon::analysis)

add_truesight_test(test_preset_conversion test_preset_conversion.cpp AudioPluginsCommon::analysis)
target_compile_definitions(test_preset_conversion PRIVATE
    TRUESIGHT_PRESETS_DIR="${CMAKE_SOURCE_DIR}/Presets")

add_truesight_test(test_loudness_golden test_loudness_golden.cpp AudioPluginsCommon::analysis)
