cmake_minimum_required(VERSION 3.22)
project(TrueSight VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# DPF has no tagged releases -- downstream projects pin an exact commit SHA on
# `main` instead. This is the same pinned commit already proven by the
# sibling Hex/Pugilist repos, which completed this same JUCE->DPF migration
# first (see AudioPlugins/Hex/CMakeLists.txt).
set(AUDIOPLUGINS_DPF_GIT_TAG "4238e1c7f0351bbe488d79f0899c540543ac7583" CACHE STRING "Pinned DPF commit")

# PATCH_COMMAND (two patches, copied verbatim from Hex/cmake/patches -- see
# each file for the full explanation and verification notes; neither reported
# upstream yet):
#
# 1. dpf-clap-state-chunked-read.patch -- PluginCLAP::stateLoad() can't tell a
#    genuine embedded NUL terminator from simply running out of bytes at a
#    chunk boundary, so a host that returns short reads from
#    clap_istream_t::read() (legal per the CLAP contract, and exactly what
#    clap-validator's `state-reproducibility-buffered` test does) can lose
#    every parameter on state load.
# 2. dpf-clap-activate-latency.patch -- PluginCLAP::activate() never reports
#    the plugin's initial latency; the first report only happens later from
#    process()/flush(), by which point the plugin is active and CLAP's
#    latency extension forbids calling changed() (must use request_restart()
#    instead) -- caught by clap-validator's transport-null/transport-fuzz/
#    transport-fuzz-sample-accurate tests.
#
# CAVEAT (CMake FetchContent): the patch step only runs on a *fresh* population of the
# source dir. If build/_deps/dpf-src already exists from an earlier configure, a plain
# reconfigure will NOT re-run it -- delete build/ (or at least build/_deps/dpf-*) when you
# need to be sure the patches actually took effect. cmake/apply_patch.cmake is idempotent
# (it skips a patch that's already applied), so a re-run is harmless, but that is not
# a substitute for a clean re-fetch.
FetchContent_Declare(dpf
    GIT_REPOSITORY https://github.com/DISTRHO/DPF.git
    GIT_TAG        ${AUDIOPLUGINS_DPF_GIT_TAG}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND}
                   -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/dpf-clap-state-chunked-read.patch
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch.cmake
            COMMAND ${CMAKE_COMMAND}
                   -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/dpf-clap-activate-latency.patch
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch.cmake
)
FetchContent_MakeAvailable(dpf)

# ResonanceWorker::ResonanceResult (100 bytes) is too large for a lock-free
# std::atomic on Linux/GCC -- libstdc++ falls back to a library-provided lock,
# which needs an explicit link against libatomic (not auto-linked by ld).
# Windows/macOS handling is deferred along with the rest of cross-platform
# packaging (see workspace CLAUDE.md). Carried over unchanged from the
# pre-DPF CMakeLists.txt (Task 3).
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
# juce_add_binary_data too. Unchanged from Task 4.
file(GLOB TRUESIGHT_PRESET_FILES "Presets/*.xml")

set(_EMBED_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp")
set(_EMBED_CPP_CONTENT "#include \"EmbeddedPresets.h\"\n\nnamespace {\n")
set(_EMBED_ENTRIES "")
foreach(_preset_file ${TRUESIGHT_PRESET_FILES})
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

set(TRUESIGHT_DPF_TARGETS vst3 clap lv2)
if(APPLE)
    list(APPEND TRUESIGHT_DPF_TARGETS au)
endif()

# Task 5 deliberately omitted UI_TYPE/FILES_UI here (no GUI sources existed
# yet -- see git history for that task's full comment explaining DPF's
# graceful UI-less fallback). Task 7 added Source/TrueSightUI.cpp as the
# plugin's first UI translation unit; Task 8 (this one) adds
# Source/UI/MasteringAdvicePanel.cpp to the same FILES_UI list.
#
# Source/Analysis/AdviceAdapter.cpp and Source/Presets/PresetManager.cpp (plus
# its generated EmbeddedPresets.cpp) live in FILES_COMMON, not FILES_DSP:
# DPF's <name>-ui static lib links PUBLIC against <name> (the FILES_COMMON
# lib) but NOT against <name>-dsp, so a symbol only compiled into FILES_DSP
# is invisible to the UI's link step. TrueSightUI.cpp (Task 8) is the first
# UI-side caller of buildAnalysisSnapshot()/buildResonancePeaks(), which
# surfaced this for AdviceAdapter.cpp -- see DPF's cmake/DPF-plugin.cmake,
# function dpf_add_plugin. The final whole-branch review (I4) found the same
# bug class in PresetManager.cpp: TrueSightUI::uiIdle() reads the preset list
# every tick via getNumPresets()/getPreset(), which only happened to link
# because those two are defined inline in PresetManager.h -- findByName(),
# refresh() and getUserPresetsDir() are out-of-line and would have reproduced
# Task 8's exact link failure on the first UI-side call. General rule going
# forward: anything the UI can reach non-virtually and non-inline must be in
# FILES_COMMON, not FILES_DSP, regardless of which "side" it conceptually
# belongs to.
dpf_add_plugin(TrueSight
    TARGETS ${TRUESIGHT_DPF_TARGETS}
    UI_TYPE opengl
    FILES_COMMON
        Source/Analysis/AdviceAdapter.cpp
        Source/Presets/PresetManager.cpp
        ${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp
    FILES_DSP
        Source/TrueSightPluginAdapter.cpp
        Source/Analysis/AnalyserEngine.cpp
        Source/Analysis/ResonanceWorker.cpp
        Source/Analysis/ResonancePeakMath.cpp
    FILES_UI
        Source/TrueSightUI.cpp
        Source/UI/MasteringAdvicePanel.cpp
)

target_include_directories(TrueSight PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source" "${CMAKE_BINARY_DIR}/generated")

# DPF's dpf_add_plugin() unconditionally compiles dgl/src/pugl.cpp's inlined
# WebViewImpl.cpp (a DPF bug: USE_FILE_BROWSER/USE_WEB_VIEW are forwarded to
# dpf__add_dgl_opengl() as raw, always-truthy "$<BOOL:...>" generator-expression
# strings -- see Hex/CMakeLists.txt's identical block for the full citation),
# which uses the GNU `typeof` extension and fails to compile under strict ISO
# C++ (our CMAKE_CXX_EXTENSIONS OFF default). Carried over verbatim from Hex,
# which hit and fixed this first. Must come after dpf_add_plugin(TrueSight
# ...) -- that call is what lazily creates the dgl-opengl target in the first
# place (see the ORDERING note on the FetchContent_MakeAvailable(AudioPluginsCommon)
# call below for the same dependency).
if(TARGET dgl-opengl)
    set_target_properties(dgl-opengl PROPERTIES CXX_EXTENSIONS ON)
endif()

# ResonanceWorker.cpp/AnalyserEngine.cpp (FILES_DSP) and AdviceAdapter.cpp/
# PresetManager.cpp (FILES_COMMON, see above) aren't built with -fPIC by
# AudioPluginsCommon's own CMakeLists.txt by
# default; linking their object files into the VST3/CLAP/LV2 shared modules
# fails with "recompile with -fPIC" otherwise. Must be set before
# FetchContent_MakeAvailable(AudioPluginsCommon) below so it applies to its
# targets too. (Ordering note, same as Hex/CMakeLists.txt: this must come
# *after* dpf_add_plugin(TrueSight ...), not merely after
# FetchContent_MakeAvailable(dpf) -- DPF creates its own targets with PIC set
# directly, so this variable only needs to be visible in time for Common's
# add_subdirectory below.)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# github.com/TriYop/spellbound-common is private -- local dev machines clone it
# over plain https using the developer's own cached git/gh credentials (no
# token needed here), while CI authenticates via a short-lived `git config
# url.insteadOf` rewrite (see Hex's .github/workflows/ci.yml for the pattern).
# PATCH_COMMAND: two real compile-error fixes in Common's own v0.8.0 release
# (its brand-new SpectrumMeter/CorrelationGauge/AdviceLabel widgets, added in
# that same tag), not anything wrong with this repo's code. Root cause (see
# each patch file for the full writeup): common/hui/dgl "only builds when the
# consuming project's CMakeLists.txt calls dpf_add_plugin() ... before
# FetchContent'ing this repo" (Common's own CLAUDE.md) -- Common's own
# standalone build never fetches DPF, so neither CorrelationGauge.cpp nor
# AdviceLabel.cpp had ever actually been compiled by anything before this
# task (TrueSight Task 7) became their first real downstream consumer.
# Same idempotent-patch caveat as the DPF patches above: only runs on a fresh
# population of build/_deps/audiopluginscommon-src.
FetchContent_Declare(AudioPluginsCommon
    GIT_REPOSITORY https://github.com/TriYop/spellbound-common.git
    GIT_TAG        v0.8.0
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND}
                   -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/common-correlationgauge-winding-type.patch
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch.cmake
            COMMAND ${CMAKE_COMMAND}
                   -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/common-advicelabel-text-shadowing.patch
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch.cmake
)
FetchContent_MakeAvailable(AudioPluginsCommon)

# Linked into `TrueSight-ui` rather than `TrueSight`: dpf_add_plugin() splits
# the plugin into a common `TrueSight` static lib plus `TrueSight-dsp` /
# `TrueSight-ui` static libs, and TrueSightUI.cpp (the only consumer of these
# widgets) lives in `TrueSight-ui`. Linking PRIVATE here (not PUBLIC) keeps the
# GL/UI library out of the DSP-only LV2 binary, same rationale as Hex-ui's
# identical line.
target_link_libraries(TrueSight-ui PRIVATE AudioPluginsCommon::hui_dgl AudioPluginsCommon::presets)

# TrueSight keeps its own Tests/CMakeLists.txt subdirectory convention from
# Stage 4A (unlike Hex, whose tests are flat add_executable()s in its
# top-level file) -- re-added here now that the JUCE removal above dropped
# the previous enable_testing()/add_subdirectory(Tests) block.
enable_testing()
add_subdirectory(Tests)

target_link_libraries(TrueSight PUBLIC
    AudioPluginsCommon::dsp
    AudioPluginsCommon::analysis
    AudioPluginsCommon::io
    ${TRUESIGHT_ATOMIC_LIB}
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PACKAGE_DEB "Package as a Debian .deb (FHS /usr paths) instead of a portable relative-path tarball" OFF)

if(PACKAGE_DEB)
    set(_DEST_VST3    lib/vst3)
    set(_DEST_CLAP    lib/clap)
    set(_DEST_LV2     lib/lv2)
    set(_DEST_PRESETS share/truesight/presets)
else()
    set(_DEST_VST3    VST3)
    set(_DEST_CLAP    CLAP)
    set(_DEST_LV2     LV2)
    set(_DEST_PRESETS Presets)
endif()

set(_BIN "${CMAKE_BINARY_DIR}/bin")

install(DIRECTORY  "${_BIN}/TrueSight.vst3"
        DESTINATION ${_DEST_VST3}
        COMPONENT   Runtime
        USE_SOURCE_PERMISSIONS)

if(APPLE)
    install(DIRECTORY  "${_BIN}/TrueSight.clap"
            DESTINATION ${_DEST_CLAP}
            COMPONENT   Runtime
            USE_SOURCE_PERMISSIONS)
else()
    install(FILES      "${_BIN}/TrueSight.clap"
            DESTINATION ${_DEST_CLAP}
            COMPONENT   Runtime
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

install(DIRECTORY  "${_BIN}/TrueSight.lv2"
        DESTINATION ${_DEST_LV2}
        COMPONENT   Runtime
        USE_SOURCE_PERMISSIONS)

install(FILES      ${TRUESIGHT_PRESET_FILES}
        DESTINATION ${_DEST_PRESETS}
        COMPONENT   Runtime)

if(NOT PACKAGE_DEB)
    install(PROGRAMS   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install.sh"
                       "${CMAKE_CURRENT_SOURCE_DIR}/scripts/uninstall.sh"
            DESTINATION .
            COMPONENT   Runtime)
endif()

if(PACKAGE_DEB)
    # ── CPack (Debian .deb) ─────────────────────────────────────────────────
    # Configure a separate build dir and run cpack from there:
    #   cmake -B build-deb -G Ninja -DCMAKE_BUILD_TYPE=Release -DPACKAGE_DEB=ON
    #   cmake --build build-deb --parallel
    #   cd build-deb && cpack
    # Requires dpkg-dev (dpkg-shlibdeps) on the build host.
    set(CPACK_GENERATOR                    DEB)
    set(CPACK_PACKAGE_NAME                 spellbound-truesight)
    set(CPACK_PACKAGE_VENDOR               Spellbound)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY  "Realtime mix analyzer and pre-mastering advisor")
    set(CPACK_PACKAGE_VERSION              ${PROJECT_VERSION})
    set(CPACK_PACKAGING_INSTALL_PREFIX     "/usr")
    set(CPACK_STRIP_FILES                  TRUE)
    set(CPACK_PACKAGE_CHECKSUM              SHA256)
    set(CPACK_PACKAGE_CONTACT              "TriYop <dev@yvanjanet.net>")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER    "TriYop <dev@yvanjanet.net>")
    set(CPACK_DEBIAN_PACKAGE_SECTION       sound)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY      optional)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS     TRUE)
    set(CPACK_DEBIAN_FILE_NAME             DEB-DEFAULT)
else()
    # ── CPack (TGZ) ───────────────────────────────────────────────────────────
    set(CPACK_GENERATOR                 TGZ)
    set(CPACK_PACKAGE_NAME              TrueSight)
    set(CPACK_PACKAGE_VENDOR            Spellbound)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Realtime mix analyzer and pre-mastering advisor")
    set(CPACK_PACKAGE_VERSION           ${PROJECT_VERSION})
    set(CPACK_SYSTEM_NAME               linux-x86_64)
    set(CPACK_PACKAGE_FILE_NAME         "TrueSight-${PROJECT_VERSION}-linux-x86_64")
    set(CPACK_PACKAGING_INSTALL_PREFIX  "")
    set(CPACK_STRIP_FILES               TRUE)
    set(CPACK_PACKAGE_CHECKSUM          SHA256)
endif()
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")
include(CPack)
