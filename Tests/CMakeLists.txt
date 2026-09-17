# TRUESIGHT_ATOMIC_LIB (libatomic, needed by ResonanceWorker's use of
# AsyncWorker<..., ResonanceResult> on Linux/GCC) is set by the top-level
# CMakeLists.txt before this subdirectory is added.

# Accepts any number of .cpp sources followed by any number of libraries to
# link — args are sorted by whether they end in ".cpp", so callers can list
# extra framework-free source files (e.g. a math file under test) inline
# alongside the usual link targets, in any trailing order.
function(add_truesight_test name)
  set(sources "")
  set(libs "")
  foreach(arg ${ARGN})
    if(arg MATCHES "\\.cpp$")
      list(APPEND sources ${arg})
    else()
      list(APPEND libs ${arg})
    endif()
  endforeach()
  add_executable(${name} ${sources})
  target_link_libraries(${name} PRIVATE ${libs})
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

add_truesight_test(test_resonance_golden
    test_resonance_golden.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp
    AudioPluginsCommon::analysis)

add_truesight_test(test_resonance_worker
    test_resonance_worker.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonanceWorker.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp
    AudioPluginsCommon::dsp AudioPluginsCommon::analysis AudioPluginsCommon::io ${TRUESIGHT_ATOMIC_LIB})

add_truesight_test(test_crossover_golden test_crossover_golden.cpp AudioPluginsCommon::dsp)

add_truesight_test(test_advice_adapter_golden
    test_advice_adapter_golden.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/AdviceAdapter.cpp
    AudioPluginsCommon::analysis)

add_truesight_test(test_analyser_engine
    test_analyser_engine.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/AnalyserEngine.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonanceWorker.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp
    AudioPluginsCommon::dsp AudioPluginsCommon::analysis AudioPluginsCommon::io ${TRUESIGHT_ATOMIC_LIB})
