cmake_minimum_required(VERSION 3.22)
project(MixAdvice VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# AudioPluginsCommon's static libraries (e.g. common/analysis, now pulled into
# MixAdvice via the LoudnessAnalyser wrapper) aren't built with -fPIC by
# default; linking their object files into the VST3/CLAP shared modules fails
# with "recompile with -fPIC" otherwise. Must be set before FetchContent'ing
# any of the dependencies below so it applies to their targets too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
    GIT_TAG        v0.8.0
)
FetchContent_MakeAvailable(AudioPluginsCommon)

# ResonanceWorker::ResonanceResult (100 bytes) is too large for a lock-free
# std::atomic on Linux/GCC — libstdc++ falls back to a library-provided lock,
# which needs an explicit link against libatomic (not auto-linked by ld).
# Windows/macOS handling is deferred along with the rest of cross-platform
# packaging (see workspace CLAUDE.md).
if(UNIX AND NOT APPLE)
    set(TRUESIGHT_ATOMIC_LIB atomic)
else()
    set(TRUESIGHT_ATOMIC_LIB "")
endif()

# ── Built-in presets, embedded at configure time ────────────────────────────
# Framework-free replacement for juce_add_binary_data: generates a small
# EmbeddedPresets.h/.cpp pair (name -> raw XML text) under
# ${CMAKE_BINARY_DIR}/generated so PresetManager can compile in the built-in
# presets without linking juce_core. Runs at *configure* time, so a
# new/edited Presets/*.xml requires a re-configure, not just a rebuild --
# acceptable since presets already required a rebuild under the old
# juce_add_binary_data too.
file(GLOB MIXADVICE_PRESET_FILES "Presets/*.xml")

set(_EMBED_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp")
set(_EMBED_CPP_CONTENT "#include \"EmbeddedPresets.h\"\n\nnamespace {\n")
set(_EMBED_ENTRIES "")
foreach(_preset_file ${MIXADVICE_PRESET_FILES})
    get_filename_component(_name ${_preset_file} NAME_WE)
    # Preset file stems contain hyphens (e.g. "electro-90"), which aren't
    # valid in a C++ identifier -- sanitize for the generated variable name
    # while keeping the original stem as the map's string key.
    string(REPLACE "-" "_" _safe_name "${_name}")
    file(READ ${_preset_file} _xml_content)
    # Raw string delimiter ")XML(" doesn't appear in any file under Presets/,
    # so no escaping of the XML body itself is needed.
    string(APPEND _EMBED_CPP_CONTENT "constexpr const char* k${_safe_name} = R\"XML(${_xml_content})XML\";\n")
    string(APPEND _EMBED_ENTRIES "        { \"${_name}\", k${_safe_name} },\n")
endforeach()
string(APPEND _EMBED_CPP_CONTENT "\n} // namespace\n\n")
string(APPEND _EMBED_CPP_CONTENT "const std::vector<EmbeddedPreset>& getEmbeddedPresets()\n{\n")
string(APPEND _EMBED_CPP_CONTENT "    static const std::vector<EmbeddedPreset> presets = {\n${_EMBED_ENTRIES}    };\n")
string(APPEND _EMBED_CPP_CONTENT "    return presets;\n}\n")
file(WRITE ${_EMBED_CPP} "${_EMBED_CPP_CONTENT}")

file(WRITE "${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.h"
"#pragma once\n#include <string>\n#include <vector>\n\nstruct EmbeddedPreset { const char* name; const char* xmlText; };\nconst std::vector<EmbeddedPreset>& getEmbeddedPresets();\n")

enable_testing()
add_subdirectory(Tests)

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
    DESCRIPTION                 "Spellbound TrueSight — See Through Every Mix"
)

target_sources(MixAdvice PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analysis/AnalyserEngine.cpp
    Source/Analysis/ResonanceWorker.cpp
    Source/Analysis/ResonancePeakMath.cpp
    Source/Analysis/AdviceAdapter.cpp
    Source/Presets/PresetManager.cpp
    ${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp
)

target_compile_definitions(MixAdvice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(MixAdvice PRIVATE Source/ ${CMAKE_BINARY_DIR}/generated)

target_compile_features(MixAdvice PUBLIC cxx_std_20)

target_link_libraries(MixAdvice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_dsp
        clap_juce_extensions
        AudioPluginsCommon::dsp
        AudioPluginsCommon::analysis
        AudioPluginsCommon::io
        ${TRUESIGHT_ATOMIC_LIB}
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
