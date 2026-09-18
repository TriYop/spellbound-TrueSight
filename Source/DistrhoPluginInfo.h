#pragma once

#define DISTRHO_PLUGIN_NAME  "MixAdvice"
#define DISTRHO_PLUGIN_URI   "https://spellbound.audio/plugins/mixadvice"

// Required so DPF's Plugin::getUniqueId() has a non-pure default
// implementation (DistrhoPlugin.hpp only provides one when
// DISTRHO_PLUGIN_UNIQUE_ID is defined; otherwise it's `= 0` and every
// concrete Plugin subclass fails to instantiate) and so AU/VST2 have a
// 4-character identity. Carried over from the old JUCE build's
// PLUGIN_MANUFACTURER_CODE (Yjnt) / PLUGIN_CODE (Mxav) for continuity.
#define DISTRHO_PLUGIN_BRAND_ID  Yjnt
#define DISTRHO_PLUGIN_UNIQUE_ID Mxav

#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_IS_SYNTH    0
#define DISTRHO_PLUGIN_WANT_STATE  0
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 0
#define DISTRHO_PLUGIN_WANT_LATENCY 0

#define DISTRHO_PLUGIN_CLAP_ID "com.yvanjanet.mixadvice"
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "analyzer", "utility"
