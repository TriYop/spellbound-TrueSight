cmake_minimum_required(VERSION 3.22)
project(MixAdvice VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Must come AFTER FetchContent_MakeAvailable(JUCE)
set(CLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(clap-juce-extensions)

FetchContent_Declare(
    AudioPluginsCommon
    GIT_REPOSITORY https://github.com/TriYop/spellbound-common.git
    GIT_TAG        v0.7.0
)
FetchContent_MakeAvailable(AudioPluginsCommon)

enable_testing()
add_subdirectory(Tests)

file(GLOB MIXADVICE_PRESET_FILES "Presets/*.xml")
juce_add_binary_data(MixAdviceBuiltInPresets SOURCES ${MIXADVICE_PRESET_FILES})

set(MIXADVICE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MIXADVICE_FORMATS AU)
endif()

juce_add_plugin(MixAdvice
    COMPANY_NAME                "YvanJanet"
    PLUGIN_MANUFACTURER_CODE    Yjnt
    PLUGIN_CODE                 Mxav
    FORMATS                     ${MIXADVICE_FORMATS}
    PRODUCT_NAME                "MixAdvice"
    BUNDLE_ID                   "com.yvanjanet.mixadvice"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    DESCRIPTION                 "Realtime mix analyzer and pre-mastering advisor"
)

target_sources(MixAdvice PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analysis/AnalyserEngine.cpp
    Source/Analysis/LoudnessAnalyser.cpp
    Source/Analysis/ResonanceDetector.cpp
    Source/Presets/PresetManager.cpp
)

target_compile_definitions(MixAdvice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(MixAdvice PRIVATE Source/)

target_compile_features(MixAdvice PUBLIC cxx_std_20)

target_link_libraries(MixAdvice
    PRIVATE
        MixAdviceBuiltInPresets
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_dsp
        clap_juce_extensions
        AudioPluginsCommon::dsp
        AudioPluginsCommon::analysis
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Must come AFTER target_link_libraries
clap_juce_extensions_plugin(
    TARGET      MixAdvice
    CLAP_ID     "com.yvanjanet.mixadvice"
    CLAP_FEATURES audio-effect analyzer utility
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 64
)

# ── Install rules ─────────────────────────────────────────────────────────────
# Packaging targets only Release artefacts; configure with -DCMAKE_BUILD_TYPE=Release.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(_ARTS "${CMAKE_BINARY_DIR}/MixAdvice_artefacts/${CMAKE_BUILD_TYPE}")

install(DIRECTORY  "${_ARTS}/VST3/MixAdvice.vst3"
        DESTINATION VST3
        COMPONENT   Runtime
        USE_SOURCE_PERMISSIONS)

install(FILES      "${_ARTS}/CLAP/MixAdvice.clap"
        DESTINATION CLAP
        COMPONENT   Runtime
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(PROGRAMS   "${_ARTS}/Standalone/MixAdvice"
        DESTINATION bin
        COMPONENT   Runtime)

install(FILES      ${MIXADVICE_PRESET_FILES}
        DESTINATION Presets
        COMPONENT   Runtime)

install(PROGRAMS   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install.sh"
                   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/uninstall.sh"
        DESTINATION .
        COMPONENT   Runtime)

# ── CPack (TGZ) ───────────────────────────────────────────────────────────────
set(CPACK_GENERATOR                 TGZ)
set(CPACK_PACKAGE_NAME              MixAdvice)
set(CPACK_PACKAGE_VENDOR            YvanJanet)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Realtime mix analyzer and pre-mastering advisor")
set(CPACK_PACKAGE_VERSION           ${PROJECT_VERSION})
set(CPACK_SYSTEM_NAME               linux-x86_64)
set(CPACK_PACKAGE_FILE_NAME         "MixAdvice-${PROJECT_VERSION}-linux-x86_64")
set(CPACK_PACKAGING_INSTALL_PREFIX  "")   # paths in tarball are relative to package root
set(CPACK_STRIP_FILES               TRUE)
set(CPACK_PACKAGE_CHECKSUM          SHA256)
# Install only our Runtime component — excludes JUCE's own dev files.
# CPACK_INSTALL_CMAKE_PROJECTS selects a single component without creating
# per-component archives (which would add a "-Runtime" suffix to the filename).
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")
include(CPack)
