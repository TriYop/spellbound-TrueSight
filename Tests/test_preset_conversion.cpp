#include "audioplugins/common/analysis/PresetIO.h"
#include "test_runner.h"
#include <cmath>
#include <string>

// Golden test of the real bundled preset file against Common's PresetIO --
// this also proves the shipped Presets/*.xml files parse unchanged. Used to
// additionally copy the parsed fields into a local std::string-based mirror
// struct, standing in for PresetManager's old toJucePresetData() conversion
// (juce::String vs std::string) so this test itself stayed JUCE-free. That
// conversion is gone now that PresetManager exposes common::analysis::
// PresetData directly (Task 4), so the mirror struct is dead weight --
// removed, checking the parsed fields directly instead.
int main() {
    auto preset = audioplugins::common::analysis::PresetIO::load(
        std::string(TRUESIGHT_PRESETS_DIR) + "/lo_fi.xml");
    CHECK(preset.has_value());

    CHECK(preset->name == "Lo-Fi");
    CHECK_MSG(std::abs(preset->bandRmsDb[0] - (-24.2917f)) < 1e-3f, "sub band mismatch");
    CHECK_MSG(std::abs(preset->overallRmsDb - (-17.1462f)) < 1e-3f, "overall RMS mismatch");
    CHECK_MSG(std::abs(preset->overallMinCorr - 0.3143f) < 1e-3f, "overall minCorr mismatch");

    TEST_SUMMARY();
}
