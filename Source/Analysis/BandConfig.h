#pragma once
#include "audioplugins/common/analysis/BandConfig.h"

// TrueSight's canonical 7-band scheme now lives in AudioPluginsCommon,
// shared with AudioPlugins/Codex — this header just re-exposes it under the
// bare `BandConfig::` name every call site in this repo already uses.
namespace BandConfig {
    inline constexpr int numBands = audioplugins::common::analysis::BandConfig::numBands;
    inline constexpr auto crossoverHz = audioplugins::common::analysis::BandConfig::crossoverHz;
    inline constexpr auto bandNames = audioplugins::common::analysis::BandConfig::bandNames;
    inline constexpr auto bandCenterHz = audioplugins::common::analysis::BandConfig::bandCenterHz;
    inline constexpr auto bandIsShelf = audioplugins::common::analysis::BandConfig::bandIsShelf;
    inline constexpr float displayFloorDb = audioplugins::common::analysis::BandConfig::displayFloorDb;
    inline constexpr float displayCeilDb = audioplugins::common::analysis::BandConfig::displayCeilDb;
}
