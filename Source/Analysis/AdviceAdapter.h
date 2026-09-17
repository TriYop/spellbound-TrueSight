#pragma once
#include "audioplugins/common/analysis/AnalysisSnapshot.h"
#include "audioplugins/common/analysis/ResonancePeakPicker.h"
#include "AnalysisResult.h"
#include <vector>

// This header is deliberately framework-free (only touches AnalysisResult.h,
// which is plain C++ atomics -- no JUCE), so it can be included directly by
// Tests/ CTest binaries without pulling JUCE into them. TrueSight's
// juce::String-based ::PresetData conversion (toCommonPresetData()) lives as
// a small local helper in PluginEditor.cpp instead, right next to its only
// two call sites -- see that file for the JUCE-touching glue.

// Converts TrueSight's live-measured AnalysisResult::Snapshot into
// AudioPluginsCommon's AnalysisSnapshot, reproducing this UI's existing
// warm-up behavior (see this file's .cpp for the exact rationale) at the
// boundary rather than inside Common's deriveAdvice().
audioplugins::common::analysis::AnalysisSnapshot buildAnalysisSnapshot (
    const AnalysisResult::Snapshot& snap, float warmupSec);

// deriveAdvice() deliberately leaves AdviceSet::resonances empty -- callers
// wire in their own resonance data. TrueSight already has its own live
// detections (from ResonanceWorker's background analysis, published into
// AnalysisResult from the audio thread), so this just carries those over
// into Common's ResonancePeak shape for display in the exported markdown
// report (see AdviceAdapter.cpp).
std::vector<audioplugins::common::analysis::ResonancePeak> buildResonancePeaks (
    const AnalysisResult::Snapshot& snap);
