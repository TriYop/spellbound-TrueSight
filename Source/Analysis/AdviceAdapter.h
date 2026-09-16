#pragma once
#include "audioplugins/common/analysis/AnalysisSnapshot.h"
#include "audioplugins/common/analysis/PresetData.h"
#include "audioplugins/common/analysis/ResonancePeakPicker.h"
#include "AnalysisResult.h"
#include "../Presets/PresetData.h"
#include <vector>

// Converts TrueSight's live-measured AnalysisResult::Snapshot into
// AudioPluginsCommon's AnalysisSnapshot, reproducing this UI's existing
// warm-up behavior (see this file's .cpp for the exact rationale) at the
// boundary rather than inside Common's deriveAdvice().
audioplugins::common::analysis::AnalysisSnapshot buildAnalysisSnapshot (
    const AnalysisResult::Snapshot& snap, float warmupSec);

audioplugins::common::analysis::PresetData toCommonPresetData (const ::PresetData& preset);

// deriveAdvice() deliberately leaves AdviceSet::resonances empty -- callers
// wire in their own resonance data. TrueSight already has its own live
// detections (from ResonanceDetector, published into AnalysisResult), so
// this just carries those over into Common's ResonancePeak shape for
// display in the exported markdown report (see AdviceAdapter.cpp).
std::vector<audioplugins::common::analysis::ResonancePeak> buildResonancePeaks (
    const AnalysisResult::Snapshot& snap);
