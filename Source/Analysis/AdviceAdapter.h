#pragma once
#include "audioplugins/common/analysis/AnalysisSnapshot.h"
#include "audioplugins/common/analysis/PresetData.h"
#include "AnalysisResult.h"
#include "../Presets/PresetData.h"

// Converts TrueSight's live-measured AnalysisResult::Snapshot into
// AudioPluginsCommon's AnalysisSnapshot, reproducing this UI's existing
// warm-up behavior (see this file's .cpp for the exact rationale) at the
// boundary rather than inside Common's deriveAdvice().
audioplugins::common::analysis::AnalysisSnapshot buildAnalysisSnapshot (
    const AnalysisResult::Snapshot& snap, float warmupSec);

audioplugins::common::analysis::PresetData toCommonPresetData (const ::PresetData& preset);
