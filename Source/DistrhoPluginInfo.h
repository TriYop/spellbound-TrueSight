#pragma once

#define DISTRHO_PLUGIN_NAME  "TrueSight"
#define DISTRHO_PLUGIN_URI   "https://spellbound.audio/plugins/truesight"

// Required so DPF's Plugin::getUniqueId() has a non-pure default
// implementation (DistrhoPlugin.hpp only provides one when
// DISTRHO_PLUGIN_UNIQUE_ID is defined; otherwise it's `= 0` and every
// concrete Plugin subclass fails to instantiate) and so AU/VST2 have a
// 4-character identity. Shared Spellbound brand code (Spbd), matching the
// sibling Hex/Pugilist plugins, plus a 4-char consonant-clip of "TrueSight"
// (Trst) for the plugin code.
#define DISTRHO_PLUGIN_BRAND_ID  Spbd
#define DISTRHO_PLUGIN_UNIQUE_ID Trst

#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_IS_SYNTH    0
#define DISTRHO_PLUGIN_WANT_STATE  0
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 0
#define DISTRHO_PLUGIN_WANT_LATENCY 0

#define DISTRHO_PLUGIN_CLAP_ID "com.spellbound.truesight"
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "analyzer", "utility"

// Task 7 gap not called out by the plan's file list: none of these existed
// before this task because no UI translation unit did either. All three are
// load-bearing for TrueSightUI.cpp, same as Hex's DistrhoPluginInfo.h:
//  - DISTRHO_PLUGIN_HAS_UI: defaults to 0 (see DPF's DistrhoPluginChecks.h) --
//    without it, no format wires up createUI() at all.
//  - DISTRHO_UI_USE_NANOVG: selects NanoTopLevelWidget as DPF's UI base
//    (DistrhoUI.hpp's `#elif DISTRHO_UI_USE_NANOVG` branch) -- every
//    common::hui::dgl widget is a NanoSubWidget, which only attaches to a
//    Nano-capable parent; without this, onNanoDisplay() would not even be a
//    virtual to override and the widget constructors would not compile.
//  - DISTRHO_PLUGIN_WANT_DIRECT_ACCESS: gates UI::getPluginInstancePointer()
//    into existence at all (`#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS` in
//    DistrhoUI.hpp) -- TrueSightUI's fPluginPtr member needs it, exactly like
//    HexPluginAdapter's direct-access meter pattern.
#define DISTRHO_PLUGIN_HAS_UI              1
#define DISTRHO_UI_USE_NANOVG              1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS  1

// DPF defaults DISTRHO_PLUGIN_AND_UI_IN_SINGLE_OBJECT to
// DISTRHO_PLUGIN_WANT_DIRECT_ACCESS's value when unset
// (DistrhoPluginLV2export.cpp:70) -- confirmed the hard way: setting
// WANT_DIRECT_ACCESS above without this line produced a generated
// manifest.ttl whose ui:binary pointed at TrueSight_dsp.so instead of
// TrueSight_ui.so. That default is wrong here, same as Hex: this CMake build
// produces two separate LV2 modules (TrueSight_dsp.so, TrueSight_ui.so), not
// one combined object. Referenced nowhere outside DPF's LV2 export code, so
// this is safe for VST3/CLAP.
#define DISTRHO_PLUGIN_AND_UI_IN_SINGLE_OBJECT 0
