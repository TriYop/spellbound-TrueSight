#include "audioplugins/common/analysis/PresetIO.h"
#include "test_runner.h"
#include <array>
#include <cmath>
#include <string>

// Mirrors PresetManager.cpp's toJucePresetData() field-for-field mapping,
// using std::string instead of juce::String so this test stays JUCE-free.
struct PlainPresetData {
    std::string name, description;
    std::array<float, 7> bandRmsDb{}, bandMinCorr{}, bandTransientDb{};
    float overallRmsDb = 0.f, overallMinCorr = 0.f;
};

static PlainPresetData toPlain(const audioplugins::common::analysis::PresetData& p) {
    return { p.name, p.description, p.bandRmsDb, p.bandMinCorr, p.bandTransientDb,
             p.overallRmsDb, p.overallMinCorr };
}

int main() {
    // Uses the real bundled preset file so this test also proves the shipped
    // Presets/*.xml files parse unchanged under Common's PresetIO.
    auto preset = audioplugins::common::analysis::PresetIO::load(
        std::string(TRUESIGHT_PRESETS_DIR) + "/lo_fi.xml");
    CHECK(preset.has_value());

    const auto plain = toPlain(*preset);
    CHECK(plain.name == "Lo-Fi");
    CHECK_MSG(std::abs(plain.bandRmsDb[0] - (-24.2917f)) < 1e-3f, "sub band mismatch");
    CHECK_MSG(std::abs(plain.overallRmsDb - (-17.1462f)) < 1e-3f, "overall RMS mismatch");
    CHECK_MSG(std::abs(plain.overallMinCorr - 0.3143f) < 1e-3f, "overall minCorr mismatch");

    TEST_SUMMARY();
}
