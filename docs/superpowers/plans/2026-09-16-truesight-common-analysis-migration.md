# TrueSight → Common (dsp/analysis) Migration — Phase 4 Stage A Step 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate TrueSight (still on JUCE — no framework change in this plan) off its private, duplicated analysis algorithms onto `AudioPlugins/Common`'s `common/dsp`/`common/analysis` modules, and build TrueSight's `Tests/` directory from scratch (it currently has zero tests), closing out Phase 4 Stage A of the workspace's JUCE→DPF migration roadmap.

**Architecture:** Each migrated piece keeps its existing JUCE-facing surface (same class names/signatures consumed by `AnalyserEngine`/`PluginEditor`/`PluginProcessor`) but delegates its actual math to `AudioPluginsCommon`'s framework-free implementation internally — the adapter/domain split the workspace's hexagonal-architecture bar requires. Every migrated piece gets a golden-vector regression test in a new `Tests/` directory (framework-free CTest, `Common`'s `test_runner.h` convention) proving numeric equivalence with the pre-migration behavior before the old code is deleted.

**Tech Stack:** C++20, CMake + Ninja, JUCE 8.0.13 (unchanged), `AudioPluginsCommon` (FetchContent, pinned tag), pugixml (transitively, via Common), CTest.

**Spec:** `/home/yvan/Projects/AudioPlugins/Common/docs/superpowers/specs/2026-09-13-truesight-common-analysis-migration-design.md` (Stage A, item 3) and `/home/yvan/.claude/plans/analyze-current-plugins-compliance-velvety-parasol.md` (Phase 4).

## Global Constraints

- **No framework change.** TrueSight stays on JUCE 8.0.13 / `juce_add_plugin` / clap-juce-extensions exactly as today. Stage B (DPF port) is separate, later work — do not touch `PluginProcessor`'s/`PluginEditor`'s base classes, CMake `juce_add_plugin()` block, or CI in this plan.
- **Numeric equivalence is the gate, not a rewrite for its own sake.** Every task that changes DSP/analysis math ships a golden-vector test comparing old-vs-new output on a fixed synthetic signal before the old code path is deleted.
- **JUCE-specific glue stays visibly separate from Common-backed domain logic.** New adapter code (type conversion, `juce::String`↔`std::string`, `juce::AudioBuffer`↔`std::vector`) lives in clearly named `*Adapter`/conversion functions, not interleaved into DSP math.
- **`Tests/` is framework-free CTest**, copying `Common`'s `tests/test_runner.h` verbatim (`CHECK`/`CHECK_MSG`/`TEST_SUMMARY` macros) — do not link JUCE into test binaries; test the framework-free logic only (adapters are tested via their pure-data-in/pure-data-out halves, not by driving real `juce::AudioBuffer`s through a live plugin instance).
- **A GitHub issue per task** already exists (filed by the coordinating session before dispatch) in `TriYop/spellbound-TrueSight` (Tasks 2–7) and `TriYop/spellbound-common` (Task 1) — mark each started when its task begins and close it when the task's tests pass and the commit lands.
- Every task ends with a commit; run the full existing build (`cmake --build build --parallel`) and, once `Tests/` exists (from Task 2 onward), `ctest --test-dir build --output-on-failure` before every commit — the plugin must keep building and the new/existing tests must stay green throughout, not just at the end.

---

## Task 1: Extend `Common`'s `PresetIO` with an in-memory (buffer) load API

**Repo:** `AudioPlugins/Common`

TrueSight's built-in presets are embedded at compile time via JUCE's `juce_add_binary_data` (`BinaryData::getNamedResource` returns an in-memory `const char*`/size, not a file path). `common/analysis::PresetIO` currently only exposes file-path-based `load()`/`loadFromDirectory()` (both call `pugi::xml_document::load_file`) — there is no way to parse an in-memory XML buffer, which Task 3 needs for the built-in-preset path. Add it now, in `Common`, so TrueSight's `PresetManager` migration (Task 3) has zero private XML-parsing code left, matching the "shared parsing logic, no duplication" point of this module's existence.

**Files:**
- Modify: `Common/include/audioplugins/common/analysis/PresetIO.h`
- Modify: `Common/src/analysis/PresetIO.cpp`
- Create: `Common/tests/test_preset_io_buffer.cpp`
- Modify: `Common/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `audioplugins::common::analysis::PresetIO::loadFromBuffer(const std::string& xmlText, std::string* errOut = nullptr) -> std::optional<PresetData>` — same parsing rules/schema as `load()`, used directly by Task 3.

- [ ] **Step 1: Write the failing test**

```cpp
// Common/tests/test_preset_io_buffer.cpp
#include "audioplugins/common/analysis/PresetIO.h"
#include "test_runner.h"

using namespace audioplugins::common::analysis;

static const char* kValidXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<MixAdvicePreset name="Lo-Fi" description="Low Fidelity">
  <BandRmsDb sub="-24.29" lows="-25.41" lomid="-27.83" mids="-26.91" himid="-35.87" highs="-40.50" air="-57.12"/>
  <BandMinCorr sub="0.0" lows="0.0" lomid="0.0" mids="0.0" himid="0.0" highs="0.0" air="0.0"/>
  <BandTransientDb sub="5.0" lows="6.0" lomid="7.0" mids="8.0" himid="9.5" highs="11.0" air="10.0"/>
  <Overall rmsDb="-17.1462" minCorr="0.3143"/>
</MixAdvicePreset>)";

int main() {
    auto preset = PresetIO::loadFromBuffer(kValidXml);
    CHECK(preset.has_value());
    CHECK(preset->name == "Lo-Fi");
    CHECK(preset->description == "Low Fidelity");
    CHECK_MSG(std::abs(preset->bandRmsDb[0] - (-24.29f)) < 1e-3f, "sub band RMS mismatch");
    CHECK_MSG(std::abs(preset->overallRmsDb - (-17.1462f)) < 1e-3f, "overall RMS mismatch");

    std::string err;
    auto bad = PresetIO::loadFromBuffer("<NotAPreset/>", &err);
    CHECK(!bad.has_value());
    CHECK_MSG(!err.empty(), "error message should be set on schema mismatch");

    TEST_SUMMARY();
}
```

- [ ] **Step 2: Wire it into the test suite and confirm it fails to build**

Add to `Common/tests/CMakeLists.txt` (near the other `analysis` tests):

```cmake
add_common_test(test_preset_io_buffer test_preset_io_buffer.cpp AudioPluginsCommon::analysis)
```

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target test_preset_io_buffer`
Expected: FAIL — `loadFromBuffer` is not a member of `PresetIO`.

- [ ] **Step 3: Implement `loadFromBuffer`**

In `Common/include/audioplugins/common/analysis/PresetIO.h`, add alongside `load`:

```cpp
// Loads one preset from an in-memory XML buffer (same schema as load()).
// For consumers that embed presets at compile time (e.g. JUCE BinaryData)
// rather than reading them from a file.
std::optional<PresetData> loadFromBuffer(const std::string& xmlText, std::string* errOut = nullptr);
```

In `Common/src/analysis/PresetIO.cpp`, factor the existing `load()` body so both share `parsePresetNode`, and add:

```cpp
std::optional<PresetData> loadFromBuffer(const std::string& xmlText, std::string* errOut) {
    pugi::xml_document doc;
    const pugi::xml_parse_result result = doc.load_buffer(xmlText.data(), xmlText.size());
    if (!result) {
        if (errOut) *errOut = result.description();
        return std::nullopt;
    }
    PresetData p;
    if (!parsePresetNode(doc.first_child(), p, errOut))
        return std::nullopt;
    return p;
}
```

(`load()` itself is unchanged — it already calls `parsePresetNode` after `load_file`.)

- [ ] **Step 4: Run the test and confirm it passes**

Run: `cmake --build build --target test_preset_io_buffer && ctest --test-dir build -R test_preset_io_buffer --output-on-failure`
Expected: PASS, `2 passed, 0 failed`-style summary (adjust to actual `CHECK` count).

- [ ] **Step 5: Run the full Common suite to confirm no regressions**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all existing tests plus the new one pass.

- [ ] **Step 6: Bump version and tag**

Update `Common/CLAUDE.md`'s module table entry for `common/analysis` to mention `PresetIO::loadFromBuffer`, and its "Versioned independently via semver git tags (`v0.6.0` currently)" line to `v0.7.0`.

- [ ] **Step 7: Commit and tag**

```bash
git add include/audioplugins/common/analysis/PresetIO.h src/analysis/PresetIO.cpp tests/test_preset_io_buffer.cpp tests/CMakeLists.txt CLAUDE.md
git commit -m "feat(analysis): add PresetIO::loadFromBuffer for in-memory XML presets"
git tag v0.7.0
git push origin main --tags
```

---

## Task 2: TrueSight — FetchContent `AudioPluginsCommon`, scaffold `Tests/`, migrate `BandConfig`

**Repo:** `AudioPlugins/TrueSight`

Proves the end-to-end wiring (FetchContent + link + CTest) on the smallest possible piece before any real DSP migration: `BandConfig.h`'s constants, which every other task in this plan depends on being sourced from `Common`.

**Files:**
- Modify: `TrueSight/CMakeLists.txt`
- Modify: `TrueSight/Source/Analysis/BandConfig.h`
- Create: `TrueSight/Tests/test_runner.h` (copied verbatim from `Common/tests/test_runner.h`)
- Create: `TrueSight/Tests/CMakeLists.txt`
- Create: `TrueSight/Tests/test_band_config.cpp`

**Interfaces:**
- Produces: `BandConfig::numBands/crossoverHz/bandNames/bandCenterHz/bandIsShelf/displayFloorDb/displayCeilDb` — unchanged names/types/values, now sourced from `audioplugins::common::analysis::BandConfig`. Every later task's file (`AnalyserEngine`, `ResonanceDetector`, `LoudnessAnalyser`, `PresetManager`, `PluginEditor`) keeps including `"Analysis/BandConfig.h"` and using bare `BandConfig::` — no call-site changes anywhere else in the repo.

- [ ] **Step 1: Add FetchContent + CTest scaffolding to the top-level CMakeLists.txt**

In `TrueSight/CMakeLists.txt`, after the existing `clap-juce-extensions` `FetchContent_MakeAvailable` block (line ~25) and before `file(GLOB MIXADVICE_PRESET_FILES ...)`:

```cmake
FetchContent_Declare(
    AudioPluginsCommon
    GIT_REPOSITORY https://github.com/TriYop/spellbound-common.git
    GIT_TAG        v0.7.0
)
FetchContent_MakeAvailable(AudioPluginsCommon)

enable_testing()
add_subdirectory(Tests)
```

Then add `AudioPluginsCommon::dsp` and `AudioPluginsCommon::analysis` to the existing `target_link_libraries(MixAdvice ... PRIVATE ...)` block (alongside `juce_dsp`, `clap_juce_extensions`).

- [ ] **Step 2: Copy the test runner and write the scaffold test**

```bash
cp /home/yvan/Projects/AudioPlugins/Common/tests/test_runner.h /home/yvan/Projects/AudioPlugins/TrueSight/Tests/test_runner.h
```

```cpp
// TrueSight/Tests/test_band_config.cpp
#include "Analysis/BandConfig.h"
#include "test_runner.h"
#include <cstring>

int main() {
    CHECK(BandConfig::numBands == 7);
    CHECK_MSG(std::abs(BandConfig::crossoverHz[0] - 80.f) < 1e-6f, "first crossover should be 80 Hz");
    CHECK_MSG(std::abs(BandConfig::crossoverHz[5] - 16000.f) < 1e-6f, "last crossover should be 16 kHz");
    CHECK_MSG(std::strcmp(BandConfig::bandNames[0], "Sub") == 0, "band 0 should be Sub");
    CHECK_MSG(std::strcmp(BandConfig::bandNames[6], "Air") == 0, "band 6 should be Air");
    CHECK(BandConfig::bandIsShelf[0] == true);
    CHECK(BandConfig::bandIsShelf[3] == false);
    CHECK(BandConfig::bandIsShelf[6] == true);
    TEST_SUMMARY();
}
```

```cmake
# TrueSight/Tests/CMakeLists.txt
function(add_truesight_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${ARGN})
  target_include_directories(${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/Source)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_truesight_test(test_band_config test_band_config.cpp)
```

- [ ] **Step 3: Confirm the test currently fails to build (BandConfig.h still hand-rolled, not yet linked against Common — this just proves the harness wiring, not a real red/green cycle for existing values)**

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (first run fetches `AudioPluginsCommon`).
Run: `cmake --build build --target test_band_config`
Expected: builds and PASSES immediately (values are already correct — this step is the scaffolding proof, not a red step; note that in the task and move on).

- [ ] **Step 4: Migrate `BandConfig.h` to source from Common**

Replace `TrueSight/Source/Analysis/BandConfig.h` entirely:

```cpp
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
```

- [ ] **Step 5: Run the test and the full plugin build**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: `test_band_config` passes; `MixAdvice_Standalone`/`_VST3` etc. still build unchanged (BandConfig's public shape is identical).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt Source/Analysis/BandConfig.h Tests/
git commit -m "feat: fetch AudioPluginsCommon, scaffold Tests/, migrate BandConfig onto it"
```

---

## Task 3: Migrate `PresetData`/`PresetManager` onto `common::analysis::PresetIO`

**Repo:** `AudioPlugins/TrueSight`

`Source/Presets/PresetData.h` and `PresetManager.h/.cpp` duplicate the exact schema and XML tag/attribute names `common::analysis::PresetData`/`PresetIO` already implement (verified: TrueSight's `Presets/*.xml` files parse unchanged under Common's `PresetIO` — root `<MixAdvicePreset>`, `<BandRmsDb sub="..." lows="..." .../>` etc., identical attribute names). No preset XML files need to change. Only the C++ parsing code is replaced; `PresetData`'s public shape (`juce::String name/description`, `std::array<float, numBands>` fields) stays the same since every call site (`PluginEditor.cpp`, `PluginProcessor.cpp`) reads it as a plain struct.

**Files:**
- Modify: `TrueSight/Source/Presets/PresetManager.cpp` (rewrite `loadBuiltIn`/`mergeFromDirectory`/`parseXml`/`readBandArray`)
- Modify: `TrueSight/Source/Presets/PresetManager.h` (drop now-unused private parsing helpers)
- Create: `TrueSight/Tests/test_preset_conversion.cpp`
- Modify: `TrueSight/Tests/CMakeLists.txt`
- Modify: `TrueSight/CMakeLists.txt` (link `AudioPluginsCommon::analysis` into the test target group if not already global)

**Interfaces:**
- Consumes: `audioplugins::common::analysis::PresetIO::loadFromBuffer(const std::string&, std::string*) -> std::optional<PresetData>` (Task 1) and `PresetIO::loadFromDirectory(const std::string&) -> std::vector<PresetData>`.
- Produces: `toJucePresetData(const audioplugins::common::analysis::PresetData&) -> ::PresetData` — a free function in `PresetManager.cpp` (anonymous namespace is fine, but name it so Task 7/8's adapter code can reuse the inverse direction later if ever needed).

- [ ] **Step 1: Write the failing test (pure conversion logic, no JUCE)**

Since the conversion function itself will live in `PresetManager.cpp` behind JUCE includes, test the schema/value mapping at the `common::analysis::PresetIO` level directly (framework-free) plus a small standalone reimplementation of the conversion for the test — this keeps `Tests/` JUCE-free per the Global Constraints:

```cpp
// TrueSight/Tests/test_preset_conversion.cpp
#include "audioplugins/common/analysis/PresetIO.h"
#include "test_runner.h"
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
```

Add to `TrueSight/Tests/CMakeLists.txt`:

```cmake
add_truesight_test(test_preset_conversion test_preset_conversion.cpp AudioPluginsCommon::analysis)
target_compile_definitions(test_preset_conversion PRIVATE
    TRUESIGHT_PRESETS_DIR="${CMAKE_SOURCE_DIR}/Presets")
```

- [ ] **Step 2: Run and confirm it currently passes against Common's PresetIO directly (proves the schema match before touching PresetManager.cpp)**

Run: `cmake --build build --target test_preset_conversion && ctest --test-dir build -R test_preset_conversion --output-on-failure`
Expected: PASS. This is intentionally a green-first check — it validates the assumption (schema compatibility) the rest of this task depends on, before any production code changes.

- [ ] **Step 3: Rewrite `PresetManager.cpp` to delegate to `PresetIO`**

```cpp
// TrueSight/Source/Presets/PresetManager.cpp
#include "PresetManager.h"
#include <BinaryData.h>
#include "audioplugins/common/analysis/PresetIO.h"

namespace {
using CommonPresetIO = audioplugins::common::analysis::PresetIO;

PresetData toJucePresetData (const audioplugins::common::analysis::PresetData& p)
{
    PresetData out;
    out.name            = juce::String (p.name);
    out.description     = juce::String (p.description);
    out.bandRmsDb        = p.bandRmsDb;
    out.bandMinCorr      = p.bandMinCorr;
    out.bandTransientDb  = p.bandTransientDb;
    out.overallRmsDb     = p.overallRmsDb;
    out.overallMinCorr   = p.overallMinCorr;
    return out;
}
} // namespace

void PresetManager::loadBuiltIn()
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* resourceName = BinaryData::namedResourceList[i];
        int         dataSize     = 0;
        const char* data         = BinaryData::getNamedResource (resourceName, dataSize);

        if (data == nullptr || dataSize <= 0)
            continue;

        std::string err;
        const std::string xmlText (data, static_cast<size_t> (dataSize));
        if (auto preset = CommonPresetIO::loadFromBuffer (xmlText, &err))
            presets_.push_back (toJucePresetData (*preset));
        else
            DBG ("PresetManager: failed to parse built-in resource: " << resourceName << " (" << err << ")");
    }
}

void PresetManager::mergeFromDirectory (juce::File dir)
{
    if (! dir.isDirectory())
        return;

    for (const auto& commonPreset : CommonPresetIO::loadFromDirectory (dir.getFullPathName().toStdString()))
    {
        auto preset = toJucePresetData (commonPreset);
        auto it = std::find_if (presets_.begin(), presets_.end(),
                                [&] (const PresetData& p) { return p.name == preset.name; });
        if (it != presets_.end())
            *it = std::move (preset);
        else
            presets_.push_back (std::move (preset));
    }
}

PresetManager::PresetManager()
{
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());
    std::sort (presets_.begin(), presets_.end(),
               [] (const PresetData& a, const PresetData& b) { return a.name < b.name; });
}

void PresetManager::refresh()
{
    presets_.clear();
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());
    std::sort (presets_.begin(), presets_.end(),
               [] (const PresetData& a, const PresetData& b) { return a.name < b.name; });
}

juce::File PresetManager::getUserPresetsDir() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MixAdvice")
                   .getChildFile ("Presets");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

int PresetManager::findByName (const juce::String& name) const
{
    for (int i = 0; i < static_cast<int> (presets_.size()); ++i)
        if (presets_[static_cast<size_t> (i)].name == name)
            return i;
    return -1;
}
```

- [ ] **Step 4: Trim `PresetManager.h`'s now-unused private parsing helpers**

Remove the `parseXml`/`readBandArray` declarations from `TrueSight/Source/Presets/PresetManager.h` (lines 30-34 in the pre-migration version) — they no longer exist in the `.cpp`.

- [ ] **Step 5: Run the full test suite and the plugin build**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all tests pass; `MixAdvice_Standalone` still lists all 26 bundled presets identically (manual spot check: launch standalone, open the preset ComboBox, confirm the same names/count as before).

- [ ] **Step 6: Commit**

```bash
git add Source/Presets/PresetManager.h Source/Presets/PresetManager.cpp Tests/test_preset_conversion.cpp Tests/CMakeLists.txt
git commit -m "feat: migrate PresetManager onto common::analysis::PresetIO"
```

---

## Task 4: Migrate `LoudnessAnalyser` onto `common::analysis::LoudnessAnalyser`'s causal API

**Repo:** `AudioPlugins/TrueSight`

`common::analysis::LoudnessAnalyser` already implements the identical causal/streaming API TrueSight hand-rolled (`processBlock(L, R, n)` / `getLraLu()`, same 30-slot/100ms-hop ring, same causal relative-gate, same K-weighting coefficients — confirmed by reading both implementations side by side). This becomes a thin wrapper, not a rewrite.

**Files:**
- Modify: `TrueSight/Source/Analysis/LoudnessAnalyser.h`
- Modify: `TrueSight/Source/Analysis/LoudnessAnalyser.cpp`
- Create: `TrueSight/Tests/test_loudness_golden.cpp`
- Modify: `TrueSight/Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `audioplugins::common::analysis::LoudnessAnalyser::prepare(double sampleRate, int numChannels)`, `.reset()`, `.processBlock(const float* L, const float* R, int numSamples)`, `.getLraLu() const -> float`.
- Produces: unchanged `::LoudnessAnalyser` public API (`prepare(double)`, `reset()`, `processBlock(L, R, n)`, `getLraLu()`) — `AnalyserEngine` (Task 6) needs no changes for this task.

- [ ] **Step 1: Capture a golden reference from the current (pre-migration) implementation**

Before touching production code, add a throwaway capture program to print reference values, run it, and record the printed numbers — they become the test's hardcoded expectations in Step 2.

```cpp
// TrueSight/Tests/capture_loudness_reference.cpp (temporary — delete after Step 2)
#include "../Source/Analysis/LoudnessAnalyser.h"
#include <cstdio>
#include <cmath>
#include <vector>

int main() {
    LoudnessAnalyser la;
    la.prepare(48000.0);

    // Synthetic signal: 4 seconds of -20 dBFS 1kHz sine (quiet), then 4 seconds
    // of -6 dBFS 1kHz sine (loud) — enough to fill the 3s LRA window twice over
    // with two distinct loudness levels, exercising the gate and the ring.
    const int sr = 48000;
    std::vector<float> buf(512);
    double phase = 0.0;
    const double freq = 1000.0;

    auto runSeconds = [&](double seconds, float amplitude) {
        int totalSamples = int(seconds * sr);
        for (int done = 0; done < totalSamples; done += (int)buf.size()) {
            int n = std::min((int)buf.size(), totalSamples - done);
            for (int i = 0; i < n; ++i) {
                buf[i] = amplitude * std::sin(phase);
                phase += 2.0 * M_PI * freq / sr;
            }
            la.processBlock(buf.data(), buf.data(), n);
        }
    };

    runSeconds(4.0, 0.1f);   // ~-20 dBFS
    printf("after quiet: lraLu=%f\n", la.getLraLu());
    runSeconds(4.0, 0.5f);   // ~-6 dBFS
    printf("after loud: lraLu=%f\n", la.getLraLu());
    return 0;
}
```

```cmake
# temporarily add to TrueSight/Tests/CMakeLists.txt, remove again in Step 2
add_truesight_test(capture_loudness_reference capture_loudness_reference.cpp)
```

Run: `cmake --build build --target capture_loudness_reference && ./build/Tests/capture_loudness_reference`
Record the two printed `lraLu` values exactly as printed.

- [ ] **Step 2: Delete the capture program, write the real golden-vector test using the recorded numbers**

Remove `capture_loudness_reference.cpp` and its `CMakeLists.txt` line. Write the permanent test against `common::analysis::LoudnessAnalyser` directly (framework-free — this is what `::LoudnessAnalyser` will wrap):

```cpp
// TrueSight/Tests/test_loudness_golden.cpp
#include "audioplugins/common/analysis/LoudnessAnalyser.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

using audioplugins::common::analysis::LoudnessAnalyser;

int main() {
    LoudnessAnalyser la;
    la.prepare(48000.0, 2);

    const int sr = 48000;
    std::vector<float> buf(512);
    double phase = 0.0;
    const double freq = 1000.0;

    auto runSeconds = [&](double seconds, float amplitude) {
        int totalSamples = int(seconds * sr);
        for (int done = 0; done < totalSamples; done += (int)buf.size()) {
            int n = std::min((int)buf.size(), totalSamples - done);
            for (int i = 0; i < n; ++i) {
                buf[i] = amplitude * std::sin(phase);
                phase += 2.0 * M_PI * freq / sr;
            }
            la.processBlock(buf.data(), buf.data(), n);
        }
    };

    runSeconds(4.0, 0.1f);
    // Replace with the exact value recorded from Step 1's capture run:
    CHECK_MSG(std::abs(la.getLraLu() - /*<CAPTURED_QUIET_VALUE>*/ 0.f) < 0.1f,
              "LRA after quiet segment should match the pre-migration reference");

    runSeconds(4.0, 0.5f);
    CHECK_MSG(std::abs(la.getLraLu() - /*<CAPTURED_LOUD_VALUE>*/ 0.f) < 0.1f,
              "LRA after loud segment should match the pre-migration reference");

    TEST_SUMMARY();
}
```

**Note for the implementer:** the two `/*<CAPTURED_..._VALUE>*/ 0.f` placeholders must be replaced with the actual numbers printed in Step 1 before this step is considered done — this is data captured from running code, not a design placeholder, and the test is meaningless (auto-passes near 0) until they're filled in.

```cmake
add_truesight_test(test_loudness_golden test_loudness_golden.cpp AudioPluginsCommon::analysis)
```

- [ ] **Step 3: Confirm the golden test passes against Common's implementation directly**

Run: `cmake --build build --target test_loudness_golden && ctest --test-dir build -R test_loudness_golden --output-on-failure`
Expected: PASS (this proves `common::analysis::LoudnessAnalyser` reproduces TrueSight's old numbers before any production code changes).

- [ ] **Step 4: Rewrite `::LoudnessAnalyser` as a thin wrapper**

```cpp
// TrueSight/Source/Analysis/LoudnessAnalyser.h
#pragma once
#include "audioplugins/common/analysis/LoudnessAnalyser.h"

// Thin JUCE-facing wrapper over AudioPluginsCommon's causal LRA implementation
// (ported from this exact class — see Common/CLAUDE.md's common/analysis entry).
class LoudnessAnalyser
{
public:
    void prepare (double sampleRate) noexcept { impl_.prepare (sampleRate, 2); }
    void reset() noexcept { impl_.reset(); }
    void processBlock (const float* L, const float* R, int numSamples) noexcept
    {
        impl_.processBlock (L, R, numSamples);
    }
    float getLraLu() const noexcept { return impl_.getLraLu(); }

private:
    audioplugins::common::analysis::LoudnessAnalyser impl_;
};
```

Delete `TrueSight/Source/Analysis/LoudnessAnalyser.cpp` entirely and remove it from `target_sources(MixAdvice PRIVATE ...)` in `CMakeLists.txt`.

- [ ] **Step 5: Run the full test suite and the plugin build**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all tests pass; `MixAdvice_Standalone` builds (no more `LoudnessAnalyser.cpp` source, header-only wrapper now).

- [ ] **Step 6: Commit**

```bash
git add Source/Analysis/LoudnessAnalyser.h CMakeLists.txt Tests/test_loudness_golden.cpp Tests/CMakeLists.txt
git rm Source/Analysis/LoudnessAnalyser.cpp
git commit -m "feat: migrate LoudnessAnalyser onto common::analysis::LoudnessAnalyser"
```

---

## Task 5: Migrate `ResonanceDetector`'s FFT + peak-picking core onto `common::dsp`/`common::analysis`

**Repo:** `AudioPlugins/TrueSight`

Keep the worker-thread/FIFO/windowing/Welford-averaging structure (real-time constraints, background-thread ownership) entirely local — only the FFT primitive (`juce::dsp::FFT` → `common::dsp::inplaceFft`) and the peak-picking loop (hand-rolled prefix-sum scan → `common::analysis::pickResonancePeaks`) are replaced.

**Files:**
- Modify: `TrueSight/Source/Analysis/ResonanceDetector.h`
- Modify: `TrueSight/Source/Analysis/ResonanceDetector.cpp`
- Create: `TrueSight/Tests/test_resonance_golden.cpp`
- Modify: `TrueSight/Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `audioplugins::common::dsp::inplaceFft(std::vector<float>& re, std::vector<float>& im)`; `audioplugins::common::analysis::pickResonancePeaks(const float* magDb, int numBins, float sampleRate, int fftSize, float minFreqHz, float maxFreqHz, float minQ, float prominenceDbThreshold, int maxResults) -> std::vector<ResonancePeak>` where `ResonancePeak{freqHz, q, gainDb, enabled, prominenceDb}`.
- Produces: unchanged `ResonanceDetector` public API — no call-site changes in `AnalyserEngine`.

- [ ] **Step 1: Extract the pure peak-picking math verbatim into a new framework-free file (TDD seam)**

`ResonanceDetector::peakPickAndPublish()` currently mixes pure math (magDb computation, peak-picking) with side effects (`result_.resonance*.store(...)`). Move the pure part into a new, JUCE-free `ResonancePeakMath.h/.cpp` pair — **verbatim, no behavior change yet** — so it's independently callable and testable without spinning up the real worker thread. The Common-based rewrite happens later, in Step 4; this step only relocates existing code.

```cpp
// TrueSight/Source/Analysis/ResonancePeakMath.h
#pragma once
#include <vector>

struct ResonancePeakResult { float freqHz = 0.f, q = 1.f, gainDb = 0.f; };

// Pure function: avgMagLinear (linear magnitude spectrum, halfN bins) -> up
// to kMaxResults peaks, sorted by prominence descending. Exposed for
// golden-vector testing; production code and tests call the exact same
// function.
std::vector<ResonancePeakResult> pickTrueSightResonancePeaks (
    const float* avgMagLinear, int halfN, double sampleRate, int fftSize);
```

```cpp
// TrueSight/Source/Analysis/ResonancePeakMath.cpp
// Body moved verbatim from ResonanceDetector::peakPickAndPublish()'s
// prefix-sum/candidate-scan block (pre-migration) — same constants, same
// algorithm, just relocated and reshaped as a pure function.
#include "ResonancePeakMath.h"
#include <algorithm>
#include <cmath>
#include <array>

std::vector<ResonancePeakResult> pickTrueSightResonancePeaks (
    const float* avgMagLinear, int halfN, double sampleRate, int fftSize)
{
    constexpr float kMinFreqHz    = 80.f;
    constexpr float kMaxFreqHz    = 16000.f;
    constexpr float kMinQ         = 3.f;
    constexpr int   kMaxResults   = 8;
    constexpr float kProminenceDb = 6.f;
    constexpr float kMaxGainDb    = 12.f;
    constexpr float kMinGainDb    = 3.f;

    struct Candidate { float freqHz = 0.f, q = 1.f, prominence = 0.f; };

    std::vector<float> magDb (static_cast<size_t> (halfN));
    for (int k = 0; k < halfN; ++k)
        magDb[static_cast<size_t> (k)] = 20.f * std::log10 (avgMagLinear[k] + 1e-9f);

    std::vector<double> prefixDb (static_cast<size_t> (halfN) + 1, 0.0);
    for (int k = 0; k < halfN; ++k)
        prefixDb[static_cast<size_t> (k + 1)] = prefixDb[static_cast<size_t> (k)]
                                               + static_cast<double> (magDb[static_cast<size_t> (k)]);

    const auto binToHz = [&] (int bin) { return static_cast<float> (bin) * static_cast<float> (sampleRate) / static_cast<float> (fftSize); };
    const auto hzToBin = [&] (float hz) { return static_cast<int> (hz * static_cast<float> (fftSize) / static_cast<float> (sampleRate)); };

    const int minBin = std::max (1, hzToBin (kMinFreqHz));
    const int maxBin = std::min (halfN - 2, hzToBin (kMaxFreqHz));

    std::vector<Candidate> top (static_cast<size_t> (kMaxResults));
    int topCount = 0;

    for (int k = minBin; k <= maxBin; ++k)
    {
        const float mk = magDb[static_cast<size_t> (k)];
        if (! (mk > magDb[static_cast<size_t> (k - 1)] && mk > magDb[static_cast<size_t> (k + 1)]))
            continue;

        const float freqHz = binToHz (k);
        const int lo = std::max (0, hzToBin (freqHz / std::sqrt (2.f)));
        const int hi = std::min (halfN - 1, hzToBin (freqHz * std::sqrt (2.f)));
        if (hi < lo) continue;

        const float bg = static_cast<float> (
            (prefixDb[static_cast<size_t> (hi + 1)] - prefixDb[static_cast<size_t> (lo)]) / (hi - lo + 1));
        const float prominence = mk - bg;
        if (prominence < kProminenceDb) continue;

        int left = k;
        while (left > 0 && magDb[static_cast<size_t> (left)] >= mk - 3.f) --left;
        int right = k;
        while (right < halfN - 1 && magDb[static_cast<size_t> (right)] >= mk - 3.f) ++right;

        const float bw3dB = binToHz (right) - binToHz (left);
        if (bw3dB <= 0.f) continue;
        const float q = freqHz / bw3dB;
        if (q < kMinQ) continue;

        if (topCount < kMaxResults || prominence > top[static_cast<size_t> (kMaxResults - 1)].prominence)
        {
            const int insertAt = std::min (topCount, kMaxResults - 1);
            top[static_cast<size_t> (insertAt)] = { freqHz, q, prominence };
            if (topCount < kMaxResults) ++topCount;
            for (int j = insertAt; j > 0 && top[static_cast<size_t> (j)].prominence > top[static_cast<size_t> (j - 1)].prominence; --j)
                std::swap (top[static_cast<size_t> (j)], top[static_cast<size_t> (j - 1)]);
        }
    }

    std::vector<ResonancePeakResult> out;
    for (int i = 0; i < topCount; ++i)
        out.push_back ({ top[static_cast<size_t> (i)].freqHz, top[static_cast<size_t> (i)].q,
                          -std::clamp (top[static_cast<size_t> (i)].prominence, kMinGainDb, kMaxGainDb) });
    return out;
}
```

Wire `ResonanceDetector::peakPickAndPublish()` (in `ResonanceDetector.cpp`) to call it instead of its old inline body:

```cpp
void ResonanceDetector::peakPickAndPublish() noexcept
{
    const auto peaks = pickTrueSightResonancePeaks (avgMag_.data(), kHalfN, sampleRate_, kFftSize);
    for (size_t i = 0; i < peaks.size() && i < static_cast<size_t> (kMaxResults); ++i)
    {
        result_.resonanceFreqHz[i].store (peaks[i].freqHz, std::memory_order_relaxed);
        result_.resonanceQ[i].store (peaks[i].q, std::memory_order_relaxed);
        result_.resonanceGainDb[i].store (peaks[i].gainDb, std::memory_order_relaxed);
    }
    result_.resonanceCount.store (static_cast<int> (peaks.size()), std::memory_order_relaxed);
}
```

Add `#include "ResonancePeakMath.h"` to `ResonanceDetector.cpp`, and add `Source/Analysis/ResonancePeakMath.cpp` to `target_sources(MixAdvice PRIVATE ...)` in `CMakeLists.txt`.

- [ ] **Step 2: Build and manually sanity-check no behavior change**

Run: `cmake --build build --parallel`
Expected: builds clean; this step is a pure refactor (Step 1 moved code, didn't change it) — no test exists yet to assert against, so this is a build-only checkpoint before capturing the golden reference in Step 3.

- [ ] **Step 3: Capture a golden reference from the now-extracted (still old-algorithm) function**

```cpp
// TrueSight/Tests/capture_resonance_reference.cpp (temporary — delete after Step 3b)
#include "../Source/Analysis/ResonancePeakMath.h"
#include <cstdio>
#include <cmath>
#include <vector>

int main() {
    constexpr int kFftSize = 4096, kHalfN = kFftSize / 2;
    const double sr = 48000.0;

    // Synthetic spectrum: flat noise floor + two narrow resonant peaks at
    // 1000 Hz and 4000 Hz, matching the shape peakPickAndPublish expects
    // (linear magnitude, not dB — the function converts internally).
    std::vector<float> avgMag(kHalfN, 0.01f);
    auto binOf = [&](float hz) { return int(hz * kFftSize / sr); };
    avgMag[binOf(1000.f)] = 1.0f;
    avgMag[binOf(4000.f)] = 0.5f;

    auto peaks = pickTrueSightResonancePeaks(avgMag.data(), kHalfN, sr, kFftSize);
    for (auto& p : peaks)
        printf("freqHz=%f q=%f gainDb=%f\n", p.freqHz, p.q, p.gainDb);
    return 0;
}
```

Build and run it against the just-extracted (still old-algorithm) `ResonancePeakMath.cpp`; record every printed line — this is the reference the permanent test asserts against.

- [ ] **Step 3b: Delete the capture program, write the permanent golden-vector test**

```cpp
// TrueSight/Tests/test_resonance_golden.cpp
#include "../Source/Analysis/ResonancePeakMath.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

int main() {
    constexpr int kFftSize = 4096, kHalfN = kFftSize / 2;
    const double sr = 48000.0;

    std::vector<float> avgMag(kHalfN, 0.01f);
    auto binOf = [&](float hz) { return int(hz * kFftSize / sr); };
    avgMag[binOf(1000.f)] = 1.0f;
    avgMag[binOf(4000.f)] = 0.5f;

    auto peaks = pickTrueSightResonancePeaks(avgMag.data(), kHalfN, sr, kFftSize);

    CHECK_MSG(peaks.size() >= 2, "expected at least 2 peaks (1kHz and 4kHz)");
    // Replace the two expected freqHz/gainDb pairs below with the exact
    // values captured in Step 3 before considering this test complete.
    CHECK_MSG(std::abs(peaks[0].freqHz - /*<CAPTURED_PEAK0_FREQ>*/ 1000.f) < 20.f, "peak 0 freq");
    CHECK_MSG(std::abs(peaks[0].gainDb - /*<CAPTURED_PEAK0_GAIN>*/ 0.f) < 0.5f, "peak 0 gain");
    CHECK_MSG(std::abs(peaks[1].freqHz - /*<CAPTURED_PEAK1_FREQ>*/ 4000.f) < 20.f, "peak 1 freq");
    CHECK_MSG(std::abs(peaks[1].gainDb - /*<CAPTURED_PEAK1_GAIN>*/ 0.f) < 0.5f, "peak 1 gain");

    TEST_SUMMARY();
}
```

```cmake
add_truesight_test(test_resonance_golden
    test_resonance_golden.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp
    AudioPluginsCommon::analysis)
```

Run: `cmake --build build --target test_resonance_golden && ctest --test-dir build -R test_resonance_golden --output-on-failure`
Expected: PASS — this confirms the test harness and captured numbers are correct while `ResonancePeakMath.cpp` still holds the old, hand-rolled algorithm.

- [ ] **Step 4: Rewrite `ResonancePeakMath.cpp`'s body to delegate to `common::analysis::pickResonancePeaks` (same header, same signature)**

```cpp
// TrueSight/Source/Analysis/ResonancePeakMath.cpp — full replacement
#include "ResonancePeakMath.h"
#include "audioplugins/common/analysis/ResonancePeakPicker.h"
#include <algorithm>
#include <cmath>

std::vector<ResonancePeakResult> pickTrueSightResonancePeaks (
    const float* avgMagLinear, int halfN, double sampleRate, int fftSize)
{
    constexpr float kMinFreqHz = 80.f, kMaxFreqHz = 16000.f, kMinQ = 3.f;
    constexpr float kProminenceDb = 6.f, kMaxGainDb = 12.f, kMinGainDb = 3.f;
    constexpr int   kMaxResults = 8;

    std::vector<float> magDb (static_cast<size_t> (halfN));
    for (int k = 0; k < halfN; ++k)
        magDb[static_cast<size_t> (k)] = 20.f * std::log10 (avgMagLinear[k] + 1e-9f);

    const auto peaks = audioplugins::common::analysis::pickResonancePeaks (
        magDb.data(), halfN, static_cast<float> (sampleRate), fftSize,
        kMinFreqHz, kMaxFreqHz, kMinQ, kProminenceDb, kMaxResults);

    std::vector<ResonancePeakResult> out;
    out.reserve (peaks.size());
    for (const auto& p : peaks)
        out.push_back ({ p.freqHz, p.q, -std::clamp (p.prominenceDb, kMinGainDb, kMaxGainDb) });
    return out;
}
```

Add `AudioPluginsCommon::analysis` to `test_resonance_golden`'s `target_link_libraries` in `Tests/CMakeLists.txt` if `add_truesight_test`'s trailing args don't already do so (they do, per Step 3b's declaration above), and link the same into `MixAdvice`'s `target_link_libraries` if not already added by Task 2.

- [ ] **Step 5: Confirm the golden test still passes against the Common-backed implementation**

Run: `cmake --build build --target test_resonance_golden && ctest --test-dir build -R test_resonance_golden --output-on-failure`
Expected: PASS — same recorded numbers, now produced by `common::analysis::pickResonancePeaks` instead of the hand-rolled scan.

- [ ] **Step 6: Swap the FFT primitive in `ResonanceDetector::run()`**

In `ResonanceDetector.h`: replace the `juce::dsp::FFT fft_ { kFftOrder };` member with two `std::vector<float>` buffers for real/imaginary parts (Common's `inplaceFft` takes `std::vector<float>&`, so use `std::vector<float>` sized once in `prepare()` rather than `std::array`, to match the reference signature without a fixed-size-to-vector copy each call):

```cpp
// replace: juce::dsp::FFT fft_ { kFftOrder };
std::vector<float> fftRe_, fftIm_;   // sized to kFftSize in prepare()
```

In `ResonanceDetector.cpp`'s `prepare()`, add `fftRe_.assign (kFftSize, 0.f); fftIm_.assign (kFftSize, 0.f);`.

Replace the FFT call in `run()`:

```cpp
// old:
//   fft_.performFrequencyOnlyForwardTransform (fftScratch_.data(), true);
// new:
std::copy (fftScratch_.begin(), fftScratch_.begin() + kFftSize, fftRe_.begin());
std::fill (fftIm_.begin(), fftIm_.end(), 0.f);
audioplugins::common::dsp::inplaceFft (fftRe_, fftIm_);
for (int k = 0; k < kHalfN; ++k)
    fftScratch_[static_cast<size_t> (k)] =
        std::sqrt (fftRe_[static_cast<size_t> (k)] * fftRe_[static_cast<size_t> (k)]
                 + fftIm_[static_cast<size_t> (k)] * fftIm_[static_cast<size_t> (k)]);
```

(`fftScratch_` already held magnitude-only output from the old JUCE call — this reproduces that shape from `inplaceFft`'s real/imaginary output, keeping every downstream line — the Welford average accumulation into `avgMag_`, and the `peakPickAndPublish()` call wired in Step 1 — unchanged.)

- [ ] **Step 7: Run the full test suite and the plugin build**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all pass. Manually smoke-test: run `MixAdvice_Standalone` with a resonant test tone and confirm the resonance panel still shows a peak near the injected frequency.

- [ ] **Step 8: Commit**

```bash
git add Source/Analysis/ResonanceDetector.h Source/Analysis/ResonanceDetector.cpp \
        Source/Analysis/ResonancePeakMath.h Source/Analysis/ResonancePeakMath.cpp \
        CMakeLists.txt Tests/test_resonance_golden.cpp Tests/CMakeLists.txt
git commit -m "feat: migrate ResonanceDetector's FFT + peak-picking onto common::dsp/common::analysis"
```

---

## Task 6: Migrate `AnalyserEngine`'s crossover filterbank onto `common::dsp::SevenBandSplitter`

**Repo:** `AudioPlugins/TrueSight`

Replaces the `std::array<juce::dsp::LinkwitzRileyFilter<float>, 6>` LP/HP cascade with `common::dsp::SevenBandSplitter`. This is the highest-risk task in the plan (different filter implementation, real-time audio-thread call site) — golden-vector tolerance is intentionally looser (RMS-level comparison, not bit-exact) since two independently-implemented 4th-order Linkwitz-Riley filters are mathematically equivalent in the passband/stopband shape but not guaranteed bit-identical.

**Files:**
- Modify: `TrueSight/Source/Analysis/AnalyserEngine.h`
- Modify: `TrueSight/Source/Analysis/AnalyserEngine.cpp`
- Create: `TrueSight/Tests/test_crossover_golden.cpp`
- Modify: `TrueSight/Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `audioplugins::common::dsp::SevenBandSplitter::prepare(float sampleRate, int numChannels)`, `.reset()`, `.process(const std::vector<std::vector<float>>& input, std::vector<std::vector<std::vector<float>>>& bands, int numFrames)` where `bands[band][channel][frame]`, `kNumBands == 7`.
- Produces: unchanged `AnalyserEngine` public API (`prepare`, `process`, `reset`, `resetPeaks`, `suspend`, `.result`) — no call-site changes in `PluginProcessor`.

- [ ] **Step 1: Capture a golden reference (per-band RMS) from the current implementation**

```cpp
// TrueSight/Tests/capture_crossover_reference.cpp (temporary — delete after Step 2)
#include "../Source/Analysis/AnalyserEngine.h"
#include <cstdio>
#include <cmath>

int main() {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    AnalyserEngine engine;
    engine.prepare(spec);

    // 2 seconds of -12 dBFS white-ish multi-tone (one tone per band center,
    // summed) so every band carries measurable energy.
    const float centers[7] = {50.f, 160.f, 375.f, 1000.f, 3500.f, 10000.f, 16000.f};
    const int sr = 48000;
    juce::AudioBuffer<float> buf(2, 512);
    std::array<double, 7> phase{};

    for (int block = 0; block < (2 * sr) / 512; ++block) {
        buf.clear();
        for (int i = 0; i < 512; ++i) {
            float sample = 0.f;
            for (int b = 0; b < 7; ++b) {
                sample += 0.1f * std::sin((float)phase[b]);
                phase[b] += 2.0 * M_PI * centers[b] / sr;
            }
            buf.setSample(0, i, sample);
            buf.setSample(1, i, sample);
        }
        engine.process(buf);
    }

    auto snap = engine.result.read();
    for (int b = 0; b < 7; ++b)
        printf("band %d: rmsDbL=%f\n", b, snap.rmsDbL[b]);
    return 0;
}
```

Build against the CURRENT (`juce::dsp::LinkwitzRileyFilter`-based) `AnalyserEngine` and run it; record all 7 printed `rmsDbL` values.

- [ ] **Step 2: Delete the capture program, write the permanent golden-vector test**

```cpp
// TrueSight/Tests/test_crossover_golden.cpp
#include "audioplugins/common/dsp/SevenBandSplitter.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

using audioplugins::common::dsp::SevenBandSplitter;

static float blockRmsDb(const std::vector<float>& v) {
    double sum = 0.0;
    for (float x : v) sum += double(x) * x;
    float rms = float(std::sqrt(sum / v.size()));
    return rms > 1e-7f ? 20.f * std::log10(rms) : -100.f;
}

int main() {
    SevenBandSplitter splitter;
    const int sr = 48000, blockSize = 512;
    splitter.prepare((float)sr, 2);

    const float centers[7] = {50.f, 160.f, 375.f, 1000.f, 3500.f, 10000.f, 16000.f};
    std::array<double, 7> phase{};

    std::vector<std::vector<float>> input(2, std::vector<float>(blockSize));
    std::vector<std::vector<std::vector<float>>> bands(
        7, std::vector<std::vector<float>>(2, std::vector<float>(blockSize)));

    // Accumulate per-band sum-of-squares across the whole 2s run for a
    // stable long-term RMS comparable to the captured reference.
    std::array<double, 7> sumSq{};
    long totalSamples = 0;

    for (int block = 0; block < (2 * sr) / blockSize; ++block) {
        for (int i = 0; i < blockSize; ++i) {
            float sample = 0.f;
            for (int b = 0; b < 7; ++b) {
                sample += 0.1f * std::sin((float)phase[b]);
                phase[b] += 2.0 * M_PI * centers[b] / sr;
            }
            input[0][i] = sample;
            input[1][i] = sample;
        }
        splitter.process(input, bands, blockSize);
        for (int b = 0; b < 7; ++b)
            for (int i = 0; i < blockSize; ++i)
                sumSq[b] += double(bands[b][0][i]) * bands[b][0][i];
        totalSamples += blockSize;
    }

    // Replace each expected value below with the corresponding rmsDbL
    // captured in Step 1 before this test is complete. Tolerance is loose
    // (2 dB) since this compares two independent LR4 implementations, not
    // bit-identical code paths -- see this task's header note.
    const float expectedDb[7] = { /*<CAPTURED_BAND0>*/ 0.f, /*<CAPTURED_BAND1>*/ 0.f,
                                   /*<CAPTURED_BAND2>*/ 0.f, /*<CAPTURED_BAND3>*/ 0.f,
                                   /*<CAPTURED_BAND4>*/ 0.f, /*<CAPTURED_BAND5>*/ 0.f,
                                   /*<CAPTURED_BAND6>*/ 0.f };

    for (int b = 0; b < 7; ++b) {
        float rms = float(std::sqrt(sumSq[b] / totalSamples));
        float db = rms > 1e-7f ? 20.f * std::log10(rms) : -100.f;
        CHECK_MSG(std::abs(db - expectedDb[b]) < 2.f, "band RMS should match pre-migration reference within 2 dB");
    }

    TEST_SUMMARY();
}
```

```cmake
add_truesight_test(test_crossover_golden test_crossover_golden.cpp AudioPluginsCommon::dsp)
```

- [ ] **Step 3: Confirm the golden test passes**

Run: `cmake --build build --target test_crossover_golden && ctest --test-dir build -R test_crossover_golden --output-on-failure`
Expected: PASS within the 2 dB tolerance. If any band fails by a wide margin (not just tolerance-edge), stop and investigate before proceeding — a large mismatch likely means a crossover-frequency or filter-order mismatch between the two implementations, not just numerical noise.

- [ ] **Step 4: Swap `AnalyserEngine`'s filterbank**

In `AnalyserEngine.h`, replace:

```cpp
std::array<juce::dsp::LinkwitzRileyFilter<float>, numCrossovers> lpFilters_;
std::array<juce::dsp::LinkwitzRileyFilter<float>, numCrossovers> hpFilters_;
juce::AudioBuffer<float> remainderBuf_;
juce::AudioBuffer<float> bandBuf_;
```

with:

```cpp
audioplugins::common::dsp::SevenBandSplitter splitter_;
// Pre-allocated once in prepare(); process() only fills these, never resizes,
// so the audio thread never allocates.
std::vector<std::vector<float>> splitterInput_;                    // [channel][frame]
std::vector<std::vector<std::vector<float>>> splitterBands_;       // [band][channel][frame]
```

and add `#include "audioplugins/common/dsp/SevenBandSplitter.h"`.

In `AnalyserEngine.cpp`'s `prepare()`, replace the crossover-filter setup loop with:

```cpp
splitter_.prepare (static_cast<float> (sr), nch);
splitterInput_.assign (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (maxBlock)));
splitterBands_.assign (static_cast<size_t> (BandConfig::numBands),
    std::vector<std::vector<float>> (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (maxBlock))));
```

In `reset()`, replace `for (auto& f : lpFilters_) f.reset(); for (auto& f : hpFilters_) f.reset();` with `splitter_.reset();`.

In `process()`, replace the "Copy input into the running remainder buffer" + "Cascaded crossover" block with:

```cpp
for (int ch = 0; ch < nChannels; ++ch)
    std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + nSamples,
               splitterInput_[static_cast<size_t> (ch)].begin());

splitter_.process (splitterInput_, splitterBands_, nSamples);

for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
    storeBand (i, splitterBands_[i][0].data(), splitterBands_[i][1].data());
```

(this replaces both the per-crossover loop and the "final band" call — `SevenBandSplitter::process` already produces all 7 bands directly, so the old cascaded-remainder bookkeeping and the separate `numCrossovers`-vs-final-band split disappear; delete `remainderBuf_`/`bandBuf_`/`monoScratch_`'s crossover-related usage, but keep `monoScratch_` — it still feeds `resonance_.pushSamples`, unrelated to this change).

- [ ] **Step 5: Run the full test suite and the plugin build**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all pass. Manually smoke-test: run `MixAdvice_Standalone` with real music playback and confirm the band-level meters still move sensibly (rough visual comparison against the pre-migration build if still available in git history).

- [ ] **Step 6: Commit**

```bash
git add Source/Analysis/AnalyserEngine.h Source/Analysis/AnalyserEngine.cpp Tests/test_crossover_golden.cpp Tests/CMakeLists.txt
git commit -m "feat: migrate AnalyserEngine's crossover filterbank onto common::dsp::SevenBandSplitter"
```

---

## Task 7: Migrate advice derivation + markdown export onto `common::analysis::AdviceSet`/`deriveAdvice`/`formatAdviceMarkdown`

**Repo:** `AudioPlugins/TrueSight`

`PluginEditor::drawAdvicePanel` and `generateMarkdown` both currently hand-compute the identical advice formulas inline (verified duplicate). Both get replaced with one shared adapter that builds a `common::analysis::AnalysisSnapshot`/`PresetData`, calls `deriveAdvice()` once, and both `drawAdvicePanel` and `exportAdvice` consume the resulting `AdviceSet`. **Behavioral nuance requiring a deliberate design choice, not a mechanical port:** the existing code blends `(avg+peak)/2` instead of `(p50+p95)/2` during a warm-up window (`secondsSinceReset < kPercentileWarmupSec`), and zeroes the LRA-based limiter-target offset when `lraLu` isn't ready yet (`snap.lraLu <= 0.f`) — `common::analysis::deriveAdvice` has neither gate (it always uses `(p50+p95)/2` and always applies the LRA offset formula, per `Common/src/analysis/AdviceSet.cpp`). Reproduce the old warm-up behavior **at the adapter boundary**, not by changing `deriveAdvice` itself: during warm-up, populate the `AnalysisSnapshot` passed into `deriveAdvice` with `p50RmsDb = avgDb, p95RmsDb = maxDb` (so the fixed `(p50+p95)/2` formula naturally reproduces the old blend) and `lraLu = 12.f` (the formula's neutral point, giving a zero offset) when `lraLu` isn't ready. This keeps `Common`'s function pure/simple while preserving TrueSight's exact existing UI behavior.

**Files:**
- Create: `TrueSight/Source/Analysis/AdviceAdapter.h`
- Create: `TrueSight/Source/Analysis/AdviceAdapter.cpp`
- Modify: `TrueSight/Source/PluginEditor.h` (drop `generateMarkdown`'s declaration; `drawAdvicePanel` signature unchanged)
- Modify: `TrueSight/Source/PluginEditor.cpp` (rewrite `drawAdvicePanel`'s body and `exportAdvice`)
- Modify: `TrueSight/CMakeLists.txt` (add `AdviceAdapter.cpp` to sources)
- Create: `TrueSight/Tests/test_advice_adapter_golden.cpp`
- Modify: `TrueSight/Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `buildAnalysisSnapshot(const AnalysisResult::Snapshot&, float kPercentileWarmupSec) -> audioplugins::common::analysis::AnalysisSnapshot` and `toCommonPresetData(const ::PresetData&) -> audioplugins::common::analysis::PresetData`, both in `AdviceAdapter.h/.cpp`. `drawAdvicePanel`/`exportAdvice` call these plus `audioplugins::common::analysis::deriveAdvice`/`formatAdviceMarkdown` directly.

- [ ] **Step 1: Capture a golden reference from the current inline formulas**

```cpp
// TrueSight/Tests/capture_advice_reference.cpp (temporary — delete after Step 2)
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <array>

// Mirrors drawAdvicePanel's exact per-band EQ formula (lines ~608-617 of the
// pre-migration PluginEditor.cpp) for one representative band, both during
// warm-up and after it, plus the limiter-target formula (lines ~747-749) both
// with and without a ready LRA reading.
int main() {
    // Band 3 (Mids), warm-up case: avgDb=-20, peakDb=-14, preset target=-18.
    {
        const float avgDb = -20.f, maxDb = -14.f, targetDb = -18.f;
        const float refDb = (avgDb + maxDb) * 0.5f;
        float eqGain = std::clamp(targetDb - refDb, -12.f, 12.f);
        if (std::abs(eqGain) < 0.5f) eqGain = 0.f;
        printf("warmup eqGain=%f\n", eqGain);
    }
    // Band 3, steady-state case: p50Db=-19, p95Db=-15, preset target=-18.
    {
        const float p50Db = -19.f, p95Db = -15.f, targetDb = -18.f;
        const float refDb = (p50Db + p95Db) * 0.5f;
        float eqGain = std::clamp(targetDb - refDb, -12.f, 12.f);
        if (std::abs(eqGain) < 0.5f) eqGain = 0.f;
        printf("steady eqGain=%f\n", eqGain);
    }
    // Limiter target, LRA not ready: preset overallRmsDb=-18.
    {
        const bool lraReady = false;
        const float lraLu = 0.f, overallRmsDb = -18.f;
        const float lraOffset = lraReady ? std::clamp((lraLu - 12.f) * 0.30f, -3.f, 3.f) : 0.f;
        printf("limiterTarget notReady=%f\n", overallRmsDb + 3.f + lraOffset);
    }
    // Limiter target, LRA ready: lraLu=15.
    {
        const bool lraReady = true;
        const float lraLu = 15.f, overallRmsDb = -18.f;
        const float lraOffset = lraReady ? std::clamp((lraLu - 12.f) * 0.30f, -3.f, 3.f) : 0.f;
        printf("limiterTarget ready=%f\n", overallRmsDb + 3.f + lraOffset);
    }
    return 0;
}
```

Compile and run standalone (`g++ -std=c++20 capture_advice_reference.cpp -o /tmp/capture && /tmp/capture`); record all 4 printed values.

- [ ] **Step 2: Delete the capture program, write the permanent golden-vector test against the new adapter + `deriveAdvice`**

```cpp
// TrueSight/Tests/test_advice_adapter_golden.cpp
#include "../Source/Analysis/AdviceAdapter.h"
#include "audioplugins/common/analysis/AdviceSet.h"
#include "test_runner.h"
#include <cmath>

int main() {
    using namespace audioplugins::common::analysis;

    PresetData preset;
    preset.bandRmsDb.fill(-18.f);
    preset.bandMinCorr.fill(0.6f);
    preset.bandTransientDb.fill(8.f);
    preset.overallRmsDb = -18.f;
    preset.overallMinCorr = 0.6f;

    // Warm-up case, band 3: avgDb=-20, peakDb=-14, secondsSinceReset below threshold.
    {
        AnalysisResult::Snapshot snap{};
        snap.avgRmsDbL[3] = -20.f; snap.avgRmsDbR[3] = -20.f;
        snap.peakRmsDbL[3] = -14.f; snap.peakRmsDbR[3] = -14.f;
        snap.secondsSinceReset = 0.5f;   // below kPercentileWarmupSec
        snap.lraLu = 0.f;                // not ready

        auto commonSnap = buildAnalysisSnapshot(snap, /*warmupSec=*/8.f);
        auto advice = deriveAdvice(commonSnap, preset);
        // Replace with the exact value captured as "warmup eqGain=..." in Step 1:
        CHECK_MSG(std::abs(advice.eq[3].gainDb - /*<CAPTURED_WARMUP_EQGAIN>*/ 0.f) < 1e-3f,
                  "warm-up eq gain should match pre-migration inline formula");
        // Replace with the exact value captured as "limiterTarget notReady=..." in Step 1:
        CHECK_MSG(std::abs(advice.limiter.targetLufsApprox - /*<CAPTURED_NOTREADY_LIMITER>*/ 0.f) < 1e-3f,
                  "limiter target should be flat (offset 0) when LRA isn't ready");
    }

    // Steady-state case, band 3: p50Db=-19, p95Db=-15, lraLu=15 (ready).
    {
        AnalysisResult::Snapshot snap{};
        snap.p50RmsDb[3] = -19.f; snap.p95RmsDb[3] = -15.f;
        snap.secondsSinceReset = 100.f;  // above kPercentileWarmupSec
        snap.lraLu = 15.f;

        auto commonSnap = buildAnalysisSnapshot(snap, /*warmupSec=*/8.f);
        auto advice = deriveAdvice(commonSnap, preset);
        // Replace with the exact value captured as "steady eqGain=..." in Step 1:
        CHECK_MSG(std::abs(advice.eq[3].gainDb - /*<CAPTURED_STEADY_EQGAIN>*/ 0.f) < 1e-3f,
                  "steady-state eq gain should match pre-migration inline formula");
        // Replace with the exact value captured as "limiterTarget ready=..." in Step 1:
        CHECK_MSG(std::abs(advice.limiter.targetLufsApprox - /*<CAPTURED_READY_LIMITER>*/ 0.f) < 1e-3f,
                  "limiter target should include the LRA offset once ready");
    }

    TEST_SUMMARY();
}
```

```cmake
add_truesight_test(test_advice_adapter_golden
    test_advice_adapter_golden.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/AdviceAdapter.cpp
    AudioPluginsCommon::analysis)
target_include_directories(test_advice_adapter_golden PRIVATE ${CMAKE_SOURCE_DIR}/Source)
```

- [ ] **Step 3: Implement the adapter**

```cpp
// TrueSight/Source/Analysis/AdviceAdapter.h
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
```

```cpp
// TrueSight/Source/Analysis/AdviceAdapter.cpp
#include "AdviceAdapter.h"
#include "BandConfig.h"

audioplugins::common::analysis::AnalysisSnapshot buildAnalysisSnapshot (
    const AnalysisResult::Snapshot& snap, float warmupSec)
{
    using audioplugins::common::analysis::AnalysisSnapshot;
    AnalysisSnapshot out;

    const bool pctReady = snap.secondsSinceReset >= warmupSec;

    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        const float avgDb = (snap.avgRmsDbL[i]  + snap.avgRmsDbR[i])  * 0.5f;
        const float maxDb = (snap.peakRmsDbL[i] + snap.peakRmsDbR[i]) * 0.5f;

        auto& band = out.bands[static_cast<size_t> (i)];
        band.avgRmsDb  = avgDb;
        band.peakRmsDb = maxDb;
        // During warm-up, feed avg/peak into the p50/p95 slots so Common's
        // fixed (p50+p95)/2 formula reproduces this UI's pre-migration
        // warm-up blend exactly -- see this task's header note.
        band.p50RmsDb    = pctReady ? snap.p50RmsDb[i] : avgDb;
        band.p95RmsDb    = pctReady ? snap.p95RmsDb[i] : maxDb;
        band.p10RmsDb    = snap.p10RmsDb[i];
        band.correlation = snap.correlation[i];
        band.crestDb     = (snap.transientDbL[i] + snap.transientDbR[i]) * 0.5f;
    }

    out.overallAvgDb  = (snap.overallRmsDbL  + snap.overallRmsDbR)  * 0.5f;
    out.overallPeakDb = (snap.peakOverallDbL + snap.peakOverallDbR) * 0.5f;
    out.overallCorr   = snap.overallCorrelation;
    // Neutral point (12 LU) zeroes deriveAdvice's LRA offset term when not
    // ready, matching the old `lraReady` gate exactly -- see header note.
    out.lraLu = snap.lraLu > 0.f ? snap.lraLu : 12.f;

    return out;
}

audioplugins::common::analysis::PresetData toCommonPresetData (const ::PresetData& preset)
{
    audioplugins::common::analysis::PresetData out;
    out.name            = preset.name.toStdString();
    out.description     = preset.description.toStdString();
    out.bandRmsDb        = preset.bandRmsDb;
    out.bandMinCorr      = preset.bandMinCorr;
    out.bandTransientDb  = preset.bandTransientDb;
    out.overallRmsDb     = preset.overallRmsDb;
    out.overallMinCorr   = preset.overallMinCorr;
    return out;
}
```

- [ ] **Step 4: Confirm the golden test passes**

Run: `cmake --build build --target test_advice_adapter_golden && ctest --test-dir build -R test_advice_adapter_golden --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Rewrite `drawAdvicePanel` and `exportAdvice`/`generateMarkdown` to use the adapter**

At the top of `drawAdvicePanel` (`PluginEditor.cpp`, after the early-return-on-no-data check), add:

```cpp
const auto commonSnap   = buildAnalysisSnapshot (snap, kPercentileWarmupSec);
const auto commonPreset = toCommonPresetData (preset);
const auto advice       = audioplugins::common::analysis::deriveAdvice (commonSnap, commonPreset);
```

Then replace every inline-computed value the drawing code reads (`eqGain`, `q`, `isShelf`, `thresh`/`ratio`/`attackMs`/`releaseMs` for both per-band and mixbus comp, `makeup`, `limiterTarget`) with the corresponding `advice.eq[i]`/`advice.mbComp[i]`/`advice.mixbusComp`/`advice.limiter` fields — the drawing/layout code itself (rects, colours, text formatting) is unchanged, only the math source changes from local computation to `advice.*`.

Replace `exportAdvice()`/`generateMarkdown()` entirely:

```cpp
void MixAdviceAudioProcessorEditor::exportAdvice()
{
    const auto snap    = processorRef.getAnalysisResult().read();
    const auto& preset = processorRef.getPresetManager().getPreset (processorRef.getCurrentProgram());

    const auto commonSnap   = buildAnalysisSnapshot (snap, kPercentileWarmupSec);
    const auto commonPreset = toCommonPresetData (preset);
    const auto advice       = audioplugins::common::analysis::deriveAdvice (commonSnap, commonPreset);
    const juce::String md   = juce::String (audioplugins::common::analysis::formatAdviceMarkdown (
        commonSnap, advice, commonPreset, preset.name.toStdString()));

    const juce::String defaultName = juce::String ("MixAdvice_")
        + juce::String (preset.name).replace (" ", "_").replace ("(", "").replace (")", "")
        + ".md";

    fileChooser_ = std::make_unique<juce::FileChooser> (
        "Save Mastering Advice",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (defaultName),
        "*.md");

    fileChooser_->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [md] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result != juce::File{})
                result.replaceWithText (md);
        });
}
```

Remove `generateMarkdown`'s declaration from `PluginEditor.h` (it no longer exists) and add `#include "Analysis/AdviceAdapter.h"` plus `#include "audioplugins/common/analysis/AdviceSet.h"` / `#include "audioplugins/common/analysis/Report.h"` to `PluginEditor.cpp`.

- [ ] **Step 6: Run the full test suite and the plugin build**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all pass. Manually smoke-test in `MixAdvice_Standalone`: play audio through both a warm-up window and past it, confirm the advice panel's numbers look sane in both regimes (no sudden jump exactly at the warm-up boundary beyond what the old code already did); click Export and confirm the markdown file's content is still a sensible mastering report (compare structure against the old hand-written format — headings/table layout may differ slightly since `formatAdviceMarkdown` is Common's own formatter, not a byte-identical port of the old `generateMarkdown` — this is expected and fine, only the underlying numbers must match).

- [ ] **Step 7: Commit**

```bash
git add Source/Analysis/AdviceAdapter.h Source/Analysis/AdviceAdapter.cpp \
        Source/PluginEditor.h Source/PluginEditor.cpp CMakeLists.txt \
        Tests/test_advice_adapter_golden.cpp Tests/CMakeLists.txt
git commit -m "feat: migrate advice derivation + markdown export onto common::analysis::deriveAdvice"
```

---

## Post-plan state (not a task — context for the executor)

After Task 7, TrueSight has zero remaining private duplicates of anything `common/dsp`/`common/analysis` provides, and a real `Tests/` directory (7 CTest targets) where none existed before. `Source/Analysis/QuantileHistogram.h` remains TrueSight-local (still used directly by `AnalyserEngine`'s own per-band P10/P50/P95 accumulation, which is not part of `common::analysis::LoudnessAnalyser` — out of scope for this plan; note it for whoever picks up Stage B or a future Common extension, don't remove it here). This closes Phase 4 Stage A step 3; Phase 4 Stage B (TrueSight's actual DPF/PluginProcessor/PluginEditor framework migration) is separate, later work per the roadmap.
