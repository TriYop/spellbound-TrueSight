# TrueSight Stage 4B: DPF Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **One GitHub issue per task**, filed in the repo the task's Files section names (TrueSight or Common) — matching Stage 4A's convention (see that stage's issues #1-#6 in `spellbound-TrueSight`).

**Goal:** Migrate TrueSight (MixAdvice) off JUCE onto DPF, replacing `PluginProcessor`/`PluginEditor` with a DPF `Plugin`/`UI` pair, moving the resonance detector's threading onto `common::io::AsyncWorker`, and building its meter/gauge/advice UI on `common::hui::dgl` widgets — closing Phase 4 of the AudioPlugins JUCE→DPF migration roadmap.

**Architecture:** Same "framework-free domain, thin DPF adapter" split already proven on Hex (effect) and Pugilist (synth): `Source/Analysis/*` and `Source/Presets/*` become pure C++20 with zero JUCE includes (Stage 4A already got them onto `common::analysis`/`common::dsp` math — this plan finishes the job by removing the remaining JUCE *threading and buffer* types they still use), and a new `MixAdvicePluginAdapter`/`MixAdviceUI` pair (naming matches Hex's `<Name>PluginAdapter`/`<Name>UI` convention) wraps them for DPF. TrueSight is an effect (`DISTRHO_PLUGIN_IS_SYNTH 0`, no Standalone target — matches Hex, and the workspace's "Standalone required" rule applies only to instruments).

**Tech Stack:** C++20, DPF (DISTRHO Plugin Framework, ISC), DGL/NanoVG, `AudioPluginsCommon` (`common::io`, `common::hui::dgl`, `common::analysis`, `common::dsp`, `common::presets`), CMake + Ninja, CTest.

**Spec:** `docs/superpowers/specs/2026-09-17-truesight-dpf-migration-design.md` (this repo). That spec's illustrative code (Sections 1.3, 2.2, 3, 4) is pseudocode written before the real signatures below were confirmed — **this plan supersedes it wherever they conflict**; see "Deviations from the design spec" below for exactly where and why.

## Global Constraints

- C++20 everywhere (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF` except the one documented DPF/GCC WebView workaround — see Task 5).
- No JUCE anywhere in the final tree — every file under `Source/` must compile with zero `juce_*` includes once this plan is done (Tests/ already achieved this in Stage 4A; Task 2-4 finish the job in `Source/`).
- `AudioPluginsCommon` is consumed via `FetchContent` pinned to an exact `GIT_TAG` (semver tag), never a branch or `file://` path (the latter broke CI once already, see `project-audioplugins-juce-dpf-migration` history) — this plan pins `v0.8.0` (cut in Task 1).
- Effect, not instrument: `DISTRHO_PLUGIN_IS_SYNTH 0`, stereo in/out only (matches the current `isBusesLayoutSupported` stereo-only gate), no MIDI, no Standalone target.
- Linux-only CI gating per the 2026-08-31 roadmap scope change: `pluginval`/`clap-validator`/`lv2lint` must pass on Linux; Windows/macOS legs run `continue-on-error` for visibility only.
- Every new framework-free class gets a CTest unit test in `Tests/`, following the existing `add_truesight_test()` CMake helper (`Tests/CMakeLists.txt`) — no new test infra invented.
- `CPACK_SYSTEM_NAME linux-x86_64`, tarball-only packaging (no `.deb` — Phase 5/6 per the roadmap).

## Deviations from the design spec

Verified against the real Common/TrueSight/Hex/Pugilist code (not assumed from the design doc's pseudocode):

1. **No `getState()`/`setState()`.** Hex declares `Plugin(kParameterCount, 0, 0)` (zero DPF states) and persists nothing beyond its automatable parameters — a preset click just calls `setParameterValue()`, and the host's normal parameter-state save/restore covers recall. TrueSight follows the same proven pattern: `presetIndex` becomes one `kParameterIsAutomatable | kParameterIsInteger` parameter (like Hex's `formula`), no binary state blob, no `common::presets::PresetIO::export/import` round-trip.
2. **`run()` takes no MIDI events.** DPF's non-synth effect signature (confirmed in `HexPluginAdapter::run`) is `run(const float** inputs, float** outputs, uint32_t frames)` — the design spec's `run(..., const MidiEvent*, uint32_t eventCount)` signature is the synth/MIDI-effect variant and doesn't apply here.
3. **`common::presets::PresetBrowser`/`Preset` don't apply to TrueSight's domain.** That module models host-parameter-value snapshots (`ParameterValue` = id/value pairs) for plugins like Hex where a "preset" is a set of knob positions. TrueSight's presets are genre band-target reference data (`common::analysis::PresetData`, already adopted in Stage 4A) with no per-preset user parameters to snapshot — there is nothing to Save/Delete. This plan reuses only the **`common::hui::dgl::PresetSelector` widget** (it only needs `vector<PresetEntry>{name, isFactory}` + an `onIndexSelected` callback — see its header) for browsing, backed directly by TrueSight's own `PresetManager`, with no Save/Delete buttons and no `PresetBrowser` controller in the loop.
4. **The on-screen "advice" panel is not `common::hui::dgl::AdviceLabel`'s categorized-message shape.** TrueSight's current `drawAdvicePanel()` (`Source/PluginEditor.cpp:551`) already calls `audioplugins::common::analysis::deriveAdvice()` and renders its numeric `AdviceSet` fields (per-band EQ gain/Q, mixbus comp thresh/ratio/attack/release, loudness/limiter target) as a data table, not categorized OK/WARN/PROBLEM sentences. Task 8 ports that numeric table to a new NanoVG widget (`MixAdviceUI`'s own, not `common/hui` — no second consumer needs it, matching Common's own design note about not adding shared widgets pre-emptively). `AdviceLabel` is still used, but only for its one legitimate fit: the "Play audio to compute mastering recommendations" warm-up message.
5. **`AsyncWorker`'s job is a fixed-size audio chunk, not pre-computed band magnitudes.** The design spec's illustrative `AnalysisRequest{ magnitudes[7], msCorrelation[7] }` doesn't match how resonance detection actually works today: `ResonanceDetector` does its own internal FFT/windowing from a continuously-fed mono sample stream (see `Source/Analysis/ResonanceDetector.cpp`). Task 3 keeps that same internal algorithm (Hann window, Welford-averaged magnitude spectrum, prefix-sum peak-pick — all already migrated onto `common::dsp`/`common::analysis` in Stage 4A) and only swaps its *threading primitive* (`juce::Thread` + `juce::AbstractFifo`) for `common::io::AsyncWorker<ResonanceJob, ResonanceResult>`, where `ResonanceJob` is one hop's worth of mono samples and `ResonanceResult` is a small POD peak list.
6. **`LoudnessAnalyser` stays on the audio thread**, exactly as today (`Source/Analysis/LoudnessAnalyser.h`'s `processBlock()` is a cheap streaming K-weighting filter, not FFT-based) — it is not part of the AsyncWorker migration despite the design spec's Section 2.2 pseudocode bundling it into `ResonanceWorker::processJob`.
7. **Project/product name stays `MixAdvice`.** The Spellbound rebrand ("TrueSight") is explicitly a separate, later pass across all 11 already-implemented plugins (see `AudioPlugins/CLAUDE.md`'s Spellbound Branding section) — this plan does not rename the CMake project, plugin class names, `CLAP_ID`, or product strings. New files use `MixAdvicePluginAdapter`/`MixAdviceUI`, matching the existing `project(MixAdvice ...)` and `CLAP_ID "com.yvanjanet.mixadvice"`.

---

## File Structure

**Common repo** (`/home/yvan/Projects/AudioPlugins/Common`):
- No new files — Task 1 only reconciles and releases what already exists on disk (`common::io::AsyncWorker`, `common::hui::dgl::{SpectrumMeter,CorrelationGauge,AdviceLabel}`, `common::analysis::PresetIO::loadFromBuffer`).

**TrueSight repo** (`/home/yvan/Projects/AudioPlugins/TrueSight`):
- Modify: `Source/Analysis/AnalyserEngine.h/.cpp` — drop JUCE buffer/spec types (Task 2).
- Rename+modify: `Source/Analysis/ResonanceDetector.h/.cpp` → `Source/Analysis/ResonanceWorker.h/.cpp` — drop `juce::Thread`/`juce::AbstractFifo` (Task 3).
- Modify: `Source/Presets/PresetManager.h/.cpp` — drop `juce::File`/`juce::String`/`BinaryData` (Task 4).
- Delete: `Source/Presets/PresetData.h` (the `juce::String` wrapper struct — callers use `audioplugins::common::analysis::PresetData` directly from here on) (Task 4).
- Create: `Source/DistrhoPluginInfo.h`, `Source/MixAdvicePluginAdapter.h/.cpp` (Task 5, extended in Task 6).
- Create: `Source/MixAdviceUI.h/.cpp` (Task 7, extended in Task 8).
- Create: `Source/UI/MasteringAdvicePanel.h/.cpp` — TrueSight-local NanoVG widget, not in Common (Task 8).
- Delete (after Task 6 lands): `Source/PluginProcessor.h/.cpp`, `Source/PluginEditor.h/.cpp` — moved to `Source/_juce_reference/` first, matching Hex/Pugilist's porting-reference convention (Task 5).
- Modify: `CMakeLists.txt` — DPF instead of JUCE (Task 5), CPack tarball (Task 10).
- Create: `Tests/test_analyser_engine.cpp`, `Tests/test_resonance_worker.cpp`, `Tests/test_preset_manager.cpp` (Tasks 2-4).
- Create: `.github/workflows/ci.yml` (Task 9).
- Create: `docs/manual-verification-stage4b.md` (Task 11).

---

### Task 1: Reconcile and release Common v0.8.0

**Repo: Common** (`/home/yvan/Projects/AudioPlugins/Common`)

**Files:**
- Modify: none (no new code — this task merges two already-complete, already-diverged commit sets)
- Test: existing `tests/` suite (full `ctest` run)

**Interfaces:**
- Consumes: nothing new.
- Produces: a pushed `master` containing both `common::io::AsyncWorker<JobType,ResultType>` and `common::hui::dgl::{SpectrumMeter,CorrelationGauge,AdviceLabel}` (local-only commits `7ef8862`/`07b2228` as of 2026-09-18) **and** `common::analysis::PresetIO::loadFromBuffer` (origin-only commit `3e9c9e0`, tagged `v0.7.0`) — plus a new tag `v0.8.0` on the merged tip. Every later task in this plan pins `GIT_TAG v0.8.0`.

**Context:** As of 2026-09-18, local `master` and `origin/master` diverged 2 commits each from a common ancestor (`b61cc0a`). Local has `7ef8862` (AsyncWorker) and `07b2228` (SpectrumMeter/CorrelationGauge/AdviceLabel + tests), never pushed. Origin has `3e9c9e0`/`19c9c50` (`PresetIO::loadFromBuffer`, merged as PR #9, tagged `v0.7.0`), never pulled locally. The two commit sets touch disjoint files except `tests/CMakeLists.txt`, which both append new `add_executable`/`add_test` lines to — a trivial, non-semantic merge conflict.

- [ ] **Step 1: Fetch and inspect the divergence**

```bash
cd /home/yvan/Projects/AudioPlugins/Common
git fetch origin
git log --oneline master..origin/master   # expect: 3e9c9e0, 19c9c50
git log --oneline origin/master..master   # expect: 7ef8862, 07b2228
```

- [ ] **Step 2: Merge origin/master into local master**

```bash
git merge origin/master -m "merge: reconcile AsyncWorker/hui-spectral-widgets with PresetIO::loadFromBuffer"
```

Resolve the expected single conflict in `tests/CMakeLists.txt` by keeping **both** sides' added test-registration blocks (one adds `test_async_worker`/`test_spectrum_meter`/`test_correlation_gauge`/`test_advice_label`, the other adds `test_preset_io_buffer` — neither side removes anything, so the resolution is a pure union of the two diffs, not a semantic decision).

- [ ] **Step 3: Full clean build + test**

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including `test_async_worker`, `test_spectrum_meter`, `test_correlation_gauge`, `test_advice_label`, and `test_preset_io_buffer` together in one build (this is the first time all five have been built and run in the same tree).

- [ ] **Step 4: Push and tag**

```bash
git push origin master
git tag v0.8.0
git push origin v0.8.0
```

- [ ] **Step 4b: Confirm no regression in Codex (the other `v0.7.0` consumer)**

`AudioPlugins/Codex` already consumes `common::analysis`/`common::dsp` (Phase 4 Stage A) — this task doesn't touch either module, but confirm that by actually re-running Codex's own suite against the new tag rather than assuming:

```bash
cd /home/yvan/Projects/AudioPlugins/Codex
# bump the pinned AudioPluginsCommon GIT_TAG to v0.8.0 in CMakeLists.txt, then:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: unchanged pass count (143 test cases / 94074 assertions per the last recorded Stage 4A run). If Codex's `CMakeLists.txt` isn't bumped as part of this task (it's a separate repo/PR), at minimum confirm the diff between `v0.7.0` and `v0.8.0` touches no file under `include/audioplugins/common/{analysis,dsp}/` or `src/{analysis,dsp}/` — satisfied here since Task 1 only adds `common/io`/`common/hui` files, per the "Files:" section above.

- [ ] **Step 5: Update Common's CLAUDE.md module table**

Add `AsyncWorker` to the `common/io` row and `SpectrumMeter`/`CorrelationGauge`/`AdviceLabel` to the `common/hui/dgl` row of the module table (they're implemented but the CLAUDE.md table predates them). Bump the "Versioned independently... (`v0.6.0` currently)" line to `v0.8.0`.

- [ ] **Step 6: Commit and push the doc update**

```bash
git add CLAUDE.md
git commit -m "docs: document AsyncWorker and spectral-analysis hui widgets, bump to v0.8.0"
git push origin master
```

---

### Task 2: Make `AnalyserEngine` framework-free

**Repo: TrueSight**

**Files:**
- Modify: `Source/Analysis/AnalyserEngine.h`, `Source/Analysis/AnalyserEngine.cpp`
- Create: `Tests/test_analyser_engine.cpp`
- Modify: `Tests/CMakeLists.txt` (register the new test)

**Interfaces:**
- Consumes: `audioplugins::common::dsp::SevenBandSplitter` (unchanged, already framework-free), `ResonanceDetector`/`resonance_` member (still that name/type until Task 3 renames it — Task 2 only changes `AnalyserEngine`'s own buffer types, not `resonance_`'s type).
- Produces: `void AnalyserEngine::prepare(double sampleRate, int maxBlockSize, int numChannels)` and `void AnalyserEngine::process(const float* const* channelData, int numChannels, int numSamples)` — the exact signature Task 6's `MixAdvicePluginAdapter::run()` calls directly with DPF's `inputs` pointer.

This is a mechanical port: `AnalyserEngine::process` today only touches JUCE via `buffer.getNumSamples()`, `buffer.getNumChannels()`, and `buffer.getReadPointer(ch)` (three call sites), plus a `juce::AudioBuffer<float> monoScratch_` member used purely as pre-sized scratch space. `prepare()` only touches JUCE via its `const juce::dsp::ProcessSpec&` parameter type. Everything else (the `storeBand` lambda, `blockRmsLinear`/`blockPeak`/`blockCorrelation`, the splitter calls) is already plain C++.

- [ ] **Step 1: Write the failing test**

```cpp
// Tests/test_analyser_engine.cpp
#include "Analysis/AnalyserEngine.h"
#include "test_runner.h"
#include <cmath>
#include <vector>

int main()
{
    AnalyserEngine engine;
    engine.prepare(48000.0, 512, 2);

    // 1 kHz sine at -6 dBFS on both channels, in-phase (correlation should be ~1).
    constexpr int n = 512;
    std::vector<float> left(n), right(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
        left[i] = s;
        right[i] = s;
    }
    const float* channels[2] = { left.data(), right.data() };

    for (int block = 0; block < 200; ++block)   // warm up the smoothers
        engine.process(channels, 2, n);

    const auto snap = engine.result.read();

    CHECK_MSG(snap.overallRmsDbL > -12.f && snap.overallRmsDbL < -3.f,
              "expected overall RMS near -6 dBFS, got " + std::to_string(snap.overallRmsDbL));
    CHECK_MSG(snap.overallCorrelation > 0.99f,
              "expected near-perfect L/R correlation for in-phase input, got "
                  + std::to_string(snap.overallCorrelation));

    TEST_SUMMARY();
}
```

- [ ] **Step 2: Register it and confirm it fails to compile (old signature)**

```cmake
# Tests/CMakeLists.txt — append
add_truesight_test(test_analyser_engine
    test_analyser_engine.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/AnalyserEngine.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonanceDetector.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp
    AudioPluginsCommon::dsp AudioPluginsCommon::analysis)
```

Run: `cmake -B build -G Ninja && cmake --build build --target test_analyser_engine`
Expected: FAIL — compile error, because `engine.prepare(48000.0, 512, 2)` doesn't match today's `prepare(const juce::dsp::ProcessSpec&)`, and this test target doesn't link JUCE at all so `AnalyserEngine.h`'s current `#include <juce_dsp/juce_dsp.h>` fails to resolve.

- [ ] **Step 3: Change the header**

```cpp
// Source/Analysis/AnalyserEngine.h
#pragma once
#include <array>
#include <vector>
#include "audioplugins/common/dsp/SevenBandSplitter.h"
#include "BandConfig.h"
#include "AnalysisResult.h"
#include "QuantileHistogram.h"
#include "LoudnessAnalyser.h"
#include "ResonanceDetector.h"

class AnalyserEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void process (const float* const* channelData, int numChannels, int numSamples);
    void reset();

    AnalysisResult result;

private:
    static float blockRmsLinear   (const float* data, int n) noexcept;
    static float blockPeak        (const float* data, int n) noexcept;
    static float blockCorrelation (const float* L, const float* R, int n) noexcept;

    audioplugins::common::dsp::SevenBandSplitter splitter_;
    std::vector<std::vector<float>> splitterInput_;
    std::vector<std::vector<std::vector<float>>> splitterBands_;

    std::array<float, BandConfig::numBands> smoothRmsL_   {};
    std::array<float, BandConfig::numBands> smoothRmsR_   {};
    std::array<float, BandConfig::numBands> smoothCorr_   {};
    std::array<float, BandConfig::numBands> smoothCrestL_ {};
    std::array<float, BandConfig::numBands> smoothCrestR_ {};

    float rmsAlpha_   { 0.9f };
    float corrAlpha_  { 0.97f };
    float crestAlpha_ { 0.97f };

    std::array<float, BandConfig::numBands> peakRmsLinL_ {};
    std::array<float, BandConfig::numBands> peakRmsLinR_ {};

    float smoothOverallL_    { 0.f };
    float smoothOverallR_    { 0.f };
    float peakOverallLinL_   { 0.f };
    float peakOverallLinR_   { 0.f };
    float smoothOverallCorr_ { 1.f };

    double intSumLR_ { 0.0 };
    double intSumL2_ { 0.0 };
    double intSumR2_ { 0.0 };

    std::array<double, BandConfig::numBands> intBandSumL2_ {};
    std::array<double, BandConfig::numBands> intBandSumR2_ {};
    uint64_t intBandBlockCount_ { 0 };

    std::array<QuantileHistogram, BandConfig::numBands> bandRmsHistograms_;

    LoudnessAnalyser loudness_;

    double   sampleRate_        { 44100.0 };
    uint64_t samplesSinceReset_ { 0 };

    // Pre-allocated mono downmix scratch buffer (was juce::AudioBuffer<float>).
    std::vector<float> monoScratch_;

    ResonanceDetector resonance_ { result };

public:
    void resetPeaks();
    void suspend();
};
```

(Only the `prepare`/`process` signatures, the removed JUCE includes, and `monoScratch_`'s type changed from the current file — every other member is copied unchanged.)

- [ ] **Step 4: Change the implementation**

```cpp
// Source/Analysis/AnalyserEngine.cpp — prepare()
void AnalyserEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    splitter_.prepare (static_cast<float> (sampleRate), numChannels);
    splitterInput_.assign (static_cast<size_t> (numChannels), std::vector<float> (static_cast<size_t> (maxBlockSize)));
    splitterBands_.assign (static_cast<size_t> (BandConfig::numBands),
        std::vector<std::vector<float>> (static_cast<size_t> (numChannels), std::vector<float> (static_cast<size_t> (maxBlockSize))));

    monoScratch_.assign (static_cast<size_t> (maxBlockSize), 0.f);

    const float blocksPerSec = static_cast<float> (sampleRate) / static_cast<float> (maxBlockSize);
    rmsAlpha_   = std::exp (-1.f / (0.10f * blocksPerSec));
    corrAlpha_  = std::exp (-1.f / (0.30f * blocksPerSec));
    crestAlpha_ = std::exp (-1.f / (0.50f * blocksPerSec));

    sampleRate_ = sampleRate;
    loudness_.prepare (sampleRate);
    resonance_.prepare (sampleRate);

    reset();
}
```

```cpp
// process() — only the signature and the three buffer-access call sites change
void AnalyserEngine::process (const float* const* channelData, int numChannels, int numSamples)
{
    const int nSamples  = numSamples;
    const int nChannels = std::min (numChannels, 2);

    if (nSamples == 0 || nChannels < 2)
        return;

    auto storeBand = [&] (size_t i, const float* L, const float* R) { /* unchanged body */ };

    {
        auto toDb = [] (float lin) { return lin > 1e-7f ? 20.f * std::log10 (lin) : -100.f; };

        const float* L = channelData[0];
        const float* R = channelData[1];

        /* unchanged body down to the mono downmix, then: */

        float* mono = monoScratch_.data();
        for (int i = 0; i < nSamples; ++i)
            mono[i] = 0.5f * (L[i] + R[i]);
        resonance_.pushSamples (mono, nSamples);
    }

    for (int ch = 0; ch < nChannels; ++ch)
        std::copy (channelData[ch], channelData[ch] + nSamples,
                   splitterInput_[static_cast<size_t> (ch)].begin());

    /* rest of the function body unchanged */
}
```

(`reset()`, `resetPeaks()`, `suspend()`, `blockRmsLinear`/`blockPeak`/`blockCorrelation` are copied verbatim from the current file — they never touched JUCE.)

- [ ] **Step 5: Build and run the test**

```bash
cmake --build build --target test_analyser_engine
ctest --test-dir build -R AnalyserEngine --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add Source/Analysis/AnalyserEngine.h Source/Analysis/AnalyserEngine.cpp \
        Tests/test_analyser_engine.cpp Tests/CMakeLists.txt
git commit -m "refactor(analysis): make AnalyserEngine framework-free (plain buffers, no juce::dsp)"
```

---

### Task 3: Port `ResonanceDetector` → `ResonanceWorker` onto `common::io::AsyncWorker`

**Repo: TrueSight**

**Files:**
- Rename+modify: `Source/Analysis/ResonanceDetector.h` → `Source/Analysis/ResonanceWorker.h`
- Rename+modify: `Source/Analysis/ResonanceDetector.cpp` → `Source/Analysis/ResonanceWorker.cpp`
- Modify: `Source/Analysis/AnalyserEngine.h` (member type/name: `resonance_` becomes a `ResonanceWorker`)
- Modify: `Tests/CMakeLists.txt`, `Tests/test_resonance_golden.cpp` (source path rename)
- Create: `Tests/test_resonance_worker.cpp`

**Interfaces:**
- Consumes: `audioplugins::common::io::AsyncWorker<JobType, ResultType>` (`v0.8.0`, Task 1) — `explicit AsyncWorker(size_t bufferSize = 16)`, `bool submit(const JobType&) noexcept`, `ResultType getLatest() const noexcept`, `void start() noexcept`, `void stop() noexcept`, and the `protected: virtual ResultType processJob(const JobType&) noexcept` a subclass overrides. `audioplugins::common::dsp::inplaceFft` and `pickTrueSightResonancePeaks` (both already used, unchanged — see `ResonancePeakMath.h`).
- Produces: `class ResonanceWorker` with the **same public API** `AnalyserEngine` already calls (`prepare(double)`, `suspend()`, `requestReset()`, `pushSamples(const float*, int)`) — Task 2's `AnalyserEngine` needs no further changes beyond the member's declared type, and `result_.resonanceFreqHz/Q/GainDb/resonanceCount` (in `AnalysisResult`) are still what gets published, so `AdviceAdapter`'s `buildResonancePeaks()` (which reads `AnalysisResult::Snapshot`) is untouched by this task.

**Context:** `ResonanceDetector` (see `Source/Analysis/ResonanceDetector.cpp`) is `private juce::Thread` and uses `juce::AbstractFifo` to hand a continuous mono sample stream from the audio thread (`pushSamples`) to its background thread (`run()`), which slides a 4096-sample analysis window by 2048-sample hops, runs an FFT every hop, and peak-picks every 5th hop. This task keeps that exact algorithm and re-homes it as `AsyncWorker<ResonanceJob, ResonanceResult>::processJob()`, where one `ResonanceJob` = one hop's worth of mono samples (`kHopSize` = 2048) — `pushSamples()` becomes the audio-thread-side code that buffers incoming samples into hop-sized chunks and calls `submit()` once per complete hop, instead of writing into a `juce::AbstractFifo`.

- [ ] **Step 1: Write the failing test**

```cpp
// Tests/test_resonance_worker.cpp
#include "Analysis/ResonanceWorker.h"
#include "test_runner.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

int main()
{
    AnalysisResult result;
    ResonanceWorker worker (result);
    worker.prepare (48000.0);

    // Feed a sustained 2 kHz tone in 512-sample chunks -- enough hops for
    // several peak-pick cycles (kPeakPickEveryNHops == 5, kHopSize == 2048).
    std::vector<float> chunk (512);
    for (int block = 0; block < 400; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = static_cast<double> (block * 512 + i) / 48000.0;
            chunk[static_cast<size_t> (i)] = 0.3f * static_cast<float> (std::sin (2.0 * 3.14159265 * 2000.0 * t));
        }
        worker.pushSamples (chunk.data(), 512);
    }

    // Give the background worker time to drain the queue and publish.
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    const auto snap = result.read();
    CHECK_MSG (snap.resonanceCount > 0, "expected at least one resonance peak published");

    bool foundNear2k = false;
    for (int i = 0; i < snap.resonanceCount; ++i)
        if (std::abs (snap.resonanceFreqHz[static_cast<size_t> (i)] - 2000.f) < 200.f)
            foundNear2k = true;
    CHECK_MSG (foundNear2k, "expected a detected peak near 2000 Hz");

    worker.suspend();
    TEST_SUMMARY();
}
```

- [ ] **Step 2: Register it, confirm it fails to compile**

```cmake
# Tests/CMakeLists.txt — append
add_truesight_test(test_resonance_worker
    test_resonance_worker.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonanceWorker.cpp
    ${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp
    AudioPluginsCommon::dsp AudioPluginsCommon::analysis AudioPluginsCommon::io)
```

Run: `cmake --build build --target test_resonance_worker`
Expected: FAIL — `Source/Analysis/ResonanceWorker.h`/`.cpp` don't exist yet.

- [ ] **Step 3: Write `ResonanceWorker.h`**

```cpp
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "audioplugins/common/io/AsyncWorker.h"
#include "AnalysisResult.h"

// Spectral resonance-cut detection. Same algorithm as before Stage 4B (Hann
// window, Welford-averaged magnitude spectrum, prefix-sum peak-pick, all
// already on common::dsp/common::analysis since Stage 4A) -- only the
// threading primitive changed, from juce::Thread + juce::AbstractFifo to
// audioplugins::common::io::AsyncWorker.
class ResonanceWorker
{
public:
    explicit ResonanceWorker (AnalysisResult& result);
    ~ResonanceWorker();

    void prepare (double sampleRate);
    void suspend();
    void requestReset() noexcept;

    // Audio-thread: buffers incoming samples into kHopSize-sized chunks and
    // submits one ResonanceJob per completed hop. Never blocks or allocates
    // (the leftover-sample staging buffer is fixed-size, pre-allocated in
    // prepare()).
    void pushSamples (const float* mono, int numSamples) noexcept;

private:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kHopSize  = kFftSize / 2;
    static constexpr int kHalfN    = kFftSize / 2;
    static constexpr int kPeakPickEveryNHops = 5;
    static constexpr int kMaxResults = AnalysisResult::maxResonances;

    struct ResonanceJob
    {
        std::array<float, kHopSize> samples {};
    };

    struct ResonanceResult
    {
        std::array<float, kMaxResults> freqHz {};
        std::array<float, kMaxResults> q      {};
        std::array<float, kMaxResults> gainDb {};
        int count = 0;
    };

    class Worker : public audioplugins::common::io::AsyncWorker<ResonanceJob, ResonanceResult>
    {
    public:
        explicit Worker (double sampleRate);
        void resetState() noexcept;
        std::atomic<bool> resetRequested { false };

    protected:
        ResonanceResult processJob (const ResonanceJob& job) noexcept override;

    private:
        double sampleRate_ { 44100.0 };
        std::vector<float> fftRe_, fftIm_;
        std::array<float, kFftSize> hannWindow_ {};
        std::array<float, kFftSize> historyBuffer_ {};
        std::array<float, 2 * kFftSize> fftScratch_ {};
        std::array<float, kHalfN> avgMag_ {};
        uint64_t windowsSinceReset_ { 0 };
        int hopsSincePeakPick_ { 0 };
    };

    AnalysisResult& result_;
    std::unique_ptr<Worker> worker_;

    // Audio-thread-only staging buffer: accumulates pushSamples() calls
    // (which may not align to kHopSize) into complete hops before submit().
    std::array<float, kHopSize> stagingBuffer_ {};
    int stagingFill_ = 0;
};
```

- [ ] **Step 4: Write `ResonanceWorker.cpp`**

```cpp
#include "ResonanceWorker.h"
#include "ResonancePeakMath.h"
#include "audioplugins/common/dsp/Fft.h"
#include <algorithm>
#include <cmath>

ResonanceWorker::Worker::Worker (double sampleRate)
    : sampleRate_ (sampleRate)
{
    for (int i = 0; i < kFftSize; ++i)
        hannWindow_[static_cast<size_t> (i)] = 0.5f * (1.f - std::cos (
            2.f * 3.14159265358979323846f * static_cast<float> (i) / static_cast<float> (kFftSize)));
    fftRe_.assign (kFftSize, 0.f);
    fftIm_.assign (kFftSize, 0.f);
    resetState();
}

void ResonanceWorker::Worker::resetState() noexcept
{
    avgMag_.fill (0.f);
    historyBuffer_.fill (0.f);
    windowsSinceReset_ = 0;
    hopsSincePeakPick_ = 0;
}

ResonanceWorker::ResonanceResult ResonanceWorker::Worker::processJob (const ResonanceJob& job) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_acq_rel))
        resetState();

    std::copy (historyBuffer_.begin() + kHopSize, historyBuffer_.end(), historyBuffer_.begin());
    std::copy (job.samples.begin(), job.samples.end(), historyBuffer_.end() - kHopSize);

    for (int i = 0; i < kFftSize; ++i)
        fftScratch_[static_cast<size_t> (i)] =
            historyBuffer_[static_cast<size_t> (i)] * hannWindow_[static_cast<size_t> (i)];
    std::fill (fftScratch_.begin() + kFftSize, fftScratch_.end(), 0.f);

    std::copy (fftScratch_.begin(), fftScratch_.begin() + kFftSize, fftRe_.begin());
    std::fill (fftIm_.begin(), fftIm_.end(), 0.f);
    audioplugins::common::dsp::inplaceFft (fftRe_, fftIm_);
    for (int k = 0; k < kHalfN; ++k)
        fftScratch_[static_cast<size_t> (k)] =
            std::sqrt (fftRe_[static_cast<size_t> (k)] * fftRe_[static_cast<size_t> (k)]
                     + fftIm_[static_cast<size_t> (k)] * fftIm_[static_cast<size_t> (k)]);

    ++windowsSinceReset_;
    for (int k = 0; k < kHalfN; ++k)
    {
        auto& avg = avgMag_[static_cast<size_t> (k)];
        avg += (fftScratch_[static_cast<size_t> (k)] - avg) / static_cast<float> (windowsSinceReset_);
    }

    ResonanceResult out;
    if (++hopsSincePeakPick_ >= kPeakPickEveryNHops)
    {
        hopsSincePeakPick_ = 0;
        const auto peaks = pickTrueSightResonancePeaks (avgMag_.data(), kHalfN, sampleRate_, kFftSize);
        out.count = std::min (static_cast<int> (peaks.size()), kMaxResults);
        for (int i = 0; i < out.count; ++i)
        {
            out.freqHz[static_cast<size_t> (i)] = peaks[static_cast<size_t> (i)].freqHz;
            out.q[static_cast<size_t> (i)]      = peaks[static_cast<size_t> (i)].q;
            out.gainDb[static_cast<size_t> (i)] = peaks[static_cast<size_t> (i)].gainDb;
        }
    }
    else
    {
        out.count = -1;   // sentinel: "no new peak-pick this hop", see pushSamples()/publish below
    }
    return out;
}

ResonanceWorker::ResonanceWorker (AnalysisResult& result) : result_ (result) {}
ResonanceWorker::~ResonanceWorker() { suspend(); }

void ResonanceWorker::prepare (double sampleRate)
{
    suspend();
    worker_ = std::make_unique<Worker> (sampleRate);
    stagingFill_ = 0;
    worker_->start();
}

void ResonanceWorker::suspend()
{
    if (worker_) worker_->stop();
}

void ResonanceWorker::requestReset() noexcept
{
    if (worker_) worker_->resetRequested.store (true, std::memory_order_release);
    result_.resonanceCount.store (0, std::memory_order_relaxed);
}

void ResonanceWorker::pushSamples (const float* mono, int numSamples) noexcept
{
    if (! worker_) return;

    int src = 0;
    while (src < numSamples)
    {
        const int toCopy = std::min (numSamples - src, kHopSize - stagingFill_);
        std::copy (mono + src, mono + src + toCopy, stagingBuffer_.begin() + stagingFill_);
        stagingFill_ += toCopy;
        src += toCopy;

        if (stagingFill_ == kHopSize)
        {
            ResonanceJob job;
            job.samples = stagingBuffer_;
            worker_->submit (job);   // never blocks; drops the hop if the queue is full
            stagingFill_ = 0;
        }
    }

    // Publish the latest peak-pick result every call (cheap: a getLatest()
    // copy of a small POD). count == -1 means "no new peak-pick since the
    // last publish" -- leave AnalysisResult untouched rather than blanking
    // the display between peak-pick cycles.
    const auto latest = worker_->getLatest();
    if (latest.count >= 0)
    {
        for (int i = 0; i < latest.count; ++i)
        {
            result_.resonanceFreqHz[static_cast<size_t> (i)].store (latest.freqHz[static_cast<size_t> (i)], std::memory_order_relaxed);
            result_.resonanceQ[static_cast<size_t> (i)].store (latest.q[static_cast<size_t> (i)], std::memory_order_relaxed);
            result_.resonanceGainDb[static_cast<size_t> (i)].store (latest.gainDb[static_cast<size_t> (i)], std::memory_order_relaxed);
        }
        result_.resonanceCount.store (latest.count, std::memory_order_relaxed);
    }
}
```

Note: `AsyncWorker<JobType,ResultType>::getLatest()` reads `std::atomic<ResultType>` — `ResonanceResult` (3 `std::array<float,8>` + 1 `int`, 100 bytes) is trivially copyable, so this compiles; it is not lock-free at that size (libstdc++ falls back to an internal lock), which is fine here since the audio thread calls `getLatest()` at most once per hop (every ~43ms at 48kHz), not every sample.

- [ ] **Step 5: Update `AnalyserEngine.h`'s member**

```cpp
// Source/Analysis/AnalyserEngine.h
#include "ResonanceWorker.h"   // was ResonanceDetector.h
/* ... */
ResonanceWorker resonance_ { result };   // was ResonanceDetector
```

- [ ] **Step 6: Update `Tests/CMakeLists.txt`'s `test_resonance_golden` source path**

`test_resonance_golden` already links `${CMAKE_SOURCE_DIR}/Source/Analysis/ResonancePeakMath.cpp` only (not `ResonanceDetector.cpp`), so no change is needed there — confirm by re-running it.

- [ ] **Step 7: Delete the old files, build, run both tests**

```bash
git rm Source/Analysis/ResonanceDetector.h Source/Analysis/ResonanceDetector.cpp
cmake --build build --target test_resonance_worker test_analyser_engine test_resonance_golden
ctest --test-dir build -R "ResonanceWorker|AnalyserEngine|resonance_golden" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add Source/Analysis/ResonanceWorker.h Source/Analysis/ResonanceWorker.cpp \
        Source/Analysis/AnalyserEngine.h Tests/test_resonance_worker.cpp Tests/CMakeLists.txt
git commit -m "refactor(analysis): replace ResonanceDetector's juce::Thread/AbstractFifo with common::io::AsyncWorker"
```

---

### Task 4: Make `PresetManager` framework-free

**Repo: TrueSight**

**Files:**
- Modify: `Source/Presets/PresetManager.h`, `Source/Presets/PresetManager.cpp`
- Delete: `Source/Presets/PresetData.h`
- Create: `Tests/test_preset_manager.cpp`
- Modify: `CMakeLists.txt` (embed `Presets/*.xml` as generated C++ instead of `juce_add_binary_data`)
- Modify: `Tests/CMakeLists.txt`, `Tests/test_preset_conversion.cpp` (that test already references `PresetData`/`PresetManager` conversion helpers that go away with this task — see Step 6)

**Interfaces:**
- Consumes: `audioplugins::common::analysis::PresetData`/`PresetIO::loadFromBuffer(xmlText, errOut)`/`PresetIO::loadFromDirectory(dirPath)` (already used in Stage 4A; `loadFromBuffer` arrives with the `v0.8.0` pin from Task 1).
- Produces: `class PresetManager` exposing `int getNumPresets() const`, `const audioplugins::common::analysis::PresetData& getPreset(int) const`, `int findByName(const std::string&) const`, `std::string getUserPresetsDir() const` — same shape as today, minus every JUCE type. Task 6's `MixAdvicePluginAdapter` and Task 7/8's UI consume `common::analysis::PresetData` directly (no more `toJucePresetData()`/`toCommonPresetData()` conversion — that round-trip existed only because JUCE's `PresetData` and Common's disagreed on `juce::String` vs `std::string`).

**Context:** `PresetManager` today loads built-in presets from `BinaryData` (JUCE's compile-time file-embedding codegen driven by `juce_add_binary_data(MixAdviceBuiltInPresets SOURCES ${MIXADVICE_PRESET_FILES})` in `CMakeLists.txt`) and merges in `juce::File`-scanned user presets, converting Common's `std::string`-based `PresetData` to a local `juce::String`-based one at both call sites (`toJucePresetData()` in `PresetManager.cpp`). This task removes the local `PresetData` wrapper entirely, uses `common::analysis::PresetData` as-is, and replaces `BinaryData` with a small CMake-generated embed (the framework-free equivalent) plus `std::filesystem` for the user directory.

- [ ] **Step 1: Write the failing test**

```cpp
// Tests/test_preset_manager.cpp
#include "Presets/PresetManager.h"
#include "test_runner.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

int main()
{
    // PresetManager reads its user dir from $HOME (see getUserPresetsDir()) --
    // point HOME at a throwaway temp dir so this test never touches the
    // developer's real ~/.config/MixAdvice/Presets.
    const auto tmpHome = std::filesystem::temp_directory_path() / "truesight_preset_manager_test";
    std::filesystem::remove_all (tmpHome);
    std::filesystem::create_directories (tmpHome / ".config" / "MixAdvice" / "Presets");
    setenv ("HOME", tmpHome.string().c_str(), 1);

    {
        std::ofstream f (tmpHome / ".config" / "MixAdvice" / "Presets" / "ZZZ_UserTest.xml");
        f << R"(<?xml version="1.0"?><MixAdvicePreset name="ZZZ_UserTest" description="test">)"
             R"(<Band index="0" rmsDb="-20" minCorr="0.5" transientDb="6"/>)"
             R"(<Overall rmsDb="-18" minCorr="0.6"/></MixAdvicePreset>)";
    }

    PresetManager mgr;
    CHECK_MSG (mgr.getNumPresets() >= 1, "expected at least the compiled-in factory presets");

    const int idx = mgr.findByName ("ZZZ_UserTest");
    CHECK_MSG (idx >= 0, "expected the user-directory preset to be discovered and merged");
    CHECK_MSG (mgr.getPreset (idx).name == "ZZZ_UserTest", "preset name round-trip mismatch");

    std::filesystem::remove_all (tmpHome);
    TEST_SUMMARY();
}
```

(The exact XML attribute schema above must match whatever `common::analysis::PresetIO::load`/`loadFromBuffer` actually parses — confirm against `Common/src/analysis/PresetIO.cpp` and one real file under `Presets/*.xml` before finalizing this fixture; adjust the literal XML if the real schema differs. This is the one place in this plan where the implementer must cross-check against the real parser rather than trust the snippet verbatim, since that source file was not read while writing this plan.)

- [ ] **Step 2: Register it, confirm it fails**

```cmake
# Tests/CMakeLists.txt
add_truesight_test(test_preset_manager
    test_preset_manager.cpp
    ${CMAKE_SOURCE_DIR}/Source/Presets/PresetManager.cpp
    ${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp
    AudioPluginsCommon::analysis)
target_include_directories(test_preset_manager PRIVATE ${CMAKE_BINARY_DIR}/generated)
```

Run: `cmake --build build --target test_preset_manager` — expect FAIL (old `PresetManager` still depends on `juce_core`/`BinaryData`, which this test target doesn't link).

- [ ] **Step 3: Add the CMake preset-embedding step**

```cmake
# CMakeLists.txt — replace the juce_add_binary_data(MixAdviceBuiltInPresets ...) block
file(GLOB MIXADVICE_PRESET_FILES "Presets/*.xml")

set(_EMBED_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp")
file(WRITE ${_EMBED_CPP} "#include \"EmbeddedPresets.h\"\n\nnamespace {\n")
set(_EMBED_ENTRIES "")
foreach(_preset_file ${MIXADVICE_PRESET_FILES})
    get_filename_component(_name ${_preset_file} NAME_WE)
    file(READ ${_preset_file} _xml_content)
    # Escape backslashes/quotes for a C++ raw string; raw strings (R"XML(...)XML")
    # need no escaping of the XML body itself, only of the delimiter collision
    # risk, which none of TrueSight's preset XML content triggers (checked: no
    # ")XML(" substring in any file under Presets/).
    string(APPEND _EMBED_CPP_CONTENT "constexpr const char* k${_name} = R\"XML(${_xml_content})XML\";\n")
    string(APPEND _EMBED_ENTRIES "        { \"${_name}\", k${_name} },\n")
endforeach()
file(APPEND ${_EMBED_CPP} "${_EMBED_CPP_CONTENT}\n} // namespace\n\n")
file(APPEND ${_EMBED_CPP} "const std::vector<EmbeddedPreset>& getEmbeddedPresets()\n{\n")
file(APPEND ${_EMBED_CPP} "    static const std::vector<EmbeddedPreset> presets = {\n${_EMBED_ENTRIES}    };\n")
file(APPEND ${_EMBED_CPP} "    return presets;\n}\n")

file(WRITE "${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.h"
"#pragma once\n#include <string>\n#include <vector>\n\nstruct EmbeddedPreset { const char* name; const char* xmlText; };\nconst std::vector<EmbeddedPreset>& getEmbeddedPresets();\n")
```

(This runs at *configure* time, so a new/edited `Presets/*.xml` requires a re-configure, not just a rebuild — acceptable since presets already required a rebuild under the old `juce_add_binary_data` too.)

- [ ] **Step 4: Rewrite `PresetManager.h`**

```cpp
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "audioplugins/common/analysis/PresetData.h"

class PresetManager
{
public:
    PresetManager();

    int getNumPresets() const noexcept { return static_cast<int> (presets_.size()); }
    const audioplugins::common::analysis::PresetData& getPreset (int i) const noexcept { return presets_[static_cast<size_t> (i)]; }

    std::string getUserPresetsDir() const;
    void refresh();
    int findByName (const std::string& name) const;

private:
    void loadBuiltIn();
    void mergeFromDirectory (const std::string& dir);

    std::vector<audioplugins::common::analysis::PresetData> presets_;
};
```

- [ ] **Step 5: Rewrite `PresetManager.cpp`**

```cpp
#include "PresetManager.h"
#include "EmbeddedPresets.h"   // generated, see CMakeLists.txt
#include "audioplugins/common/analysis/PresetIO.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace {
namespace CommonPresetIO = audioplugins::common::analysis::PresetIO;
}

void PresetManager::loadBuiltIn()
{
    for (const auto& embedded : getEmbeddedPresets())
    {
        std::string err;
        if (auto preset = CommonPresetIO::loadFromBuffer (embedded.xmlText, &err))
            presets_.push_back (*preset);
    }
}

void PresetManager::mergeFromDirectory (const std::string& dir)
{
    if (! std::filesystem::is_directory (dir))
        return;

    for (auto& preset : CommonPresetIO::loadFromDirectory (dir))
    {
        auto it = std::find_if (presets_.begin(), presets_.end(),
                                 [&] (const auto& p) { return p.name == preset.name; });
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
               [] (const auto& a, const auto& b) { return a.name < b.name; });
}

void PresetManager::refresh()
{
    presets_.clear();
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());
    std::sort (presets_.begin(), presets_.end(),
               [] (const auto& a, const auto& b) { return a.name < b.name; });
}

std::string PresetManager::getUserPresetsDir() const
{
    const char* home = std::getenv ("HOME");
    std::filesystem::path dir = (home ? std::filesystem::path (home) : std::filesystem::current_path())
                                    / ".config" / "MixAdvice" / "Presets";
    std::error_code ec;
    std::filesystem::create_directories (dir, ec);
    return dir.string();
}

int PresetManager::findByName (const std::string& name) const
{
    for (int i = 0; i < static_cast<int> (presets_.size()); ++i)
        if (presets_[static_cast<size_t> (i)].name == name)
            return i;
    return -1;
}
```

- [ ] **Step 6: Delete `PresetData.h`, update its one other consumer**

```bash
git rm Source/Presets/PresetData.h
```

`Tests/test_preset_conversion.cpp` (Stage 4A) tests `toCommonPresetData()`/`toJucePresetData()`-style conversion helpers that no longer exist once `PresetManager` returns `common::analysis::PresetData` directly — inspect that file and either delete it (if its only purpose was round-trip-testing the now-removed conversion) or repoint it at `PresetIO::load`/`loadFromDirectory` directly against the `TRUESIGHT_PRESETS_DIR` compile definition it already receives (see `Tests/CMakeLists.txt`), whichever it turns out to actually be testing — read it before deciding, since this plan was written without opening that file.

- [ ] **Step 7: Build and run**

```bash
cmake -B build -G Ninja
cmake --build build --target test_preset_manager
ctest --test-dir build -R PresetManager --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add Source/Presets/PresetManager.h Source/Presets/PresetManager.cpp \
        Tests/test_preset_manager.cpp Tests/CMakeLists.txt CMakeLists.txt
git rm Source/Presets/PresetData.h
git commit -m "refactor(presets): make PresetManager framework-free (std::filesystem, CMake-embedded presets)"
```

---

### Task 5: DPF bootstrap — CMakeLists.txt, DistrhoPluginInfo.h, minimal `MixAdvicePluginAdapter`

**Repo: TrueSight**

**Files:**
- Modify: `CMakeLists.txt` (remove JUCE/clap-juce-extensions `FetchContent`, add DPF)
- Create: `Source/DistrhoPluginInfo.h`
- Create: `Source/MixAdvicePluginAdapter.h`, `Source/MixAdvicePluginAdapter.cpp` (passthrough-only; Task 6 wires in the analysis pipeline)
- Move: `Source/PluginProcessor.h/.cpp`, `Source/PluginEditor.h/.cpp` → `Source/_juce_reference/` (porting reference, matching Hex/Pugilist's convention)

**Interfaces:**
- Consumes: DPF's `Plugin` base class (`DistrhoPlugin.hpp`), `AudioPluginsCommon` `v0.8.0`.
- Produces: `class MixAdvicePluginAdapter : public Plugin` with `enum { kParameterPresetIndex, kParameterCount };` — the one automatable parameter every later task (6, 7, 8) reads/writes by that index. Builds `MixAdvice.vst3`/`MixAdvice.clap`/`MixAdvice.lv2` under `build/bin/`.

- [ ] **Step 1: Move the JUCE reference files**

```bash
mkdir -p Source/_juce_reference
git mv Source/PluginProcessor.h Source/PluginProcessor.cpp \
       Source/PluginEditor.h Source/PluginEditor.cpp \
       Source/_juce_reference/
```

- [ ] **Step 2: Write `DistrhoPluginInfo.h`**

```cpp
#pragma once

#define DISTRHO_PLUGIN_NAME  "MixAdvice"
#define DISTRHO_PLUGIN_URI   "https://spellbound.audio/plugins/mixadvice"

#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_IS_SYNTH    0
#define DISTRHO_PLUGIN_WANT_STATE  0
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 0
#define DISTRHO_PLUGIN_WANT_LATENCY 0

#define DISTRHO_PLUGIN_CLAP_ID "com.yvanjanet.mixadvice"
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "analyzer", "utility"

#define DISTRHO_UI_USE_NANOVG 1
```

- [ ] **Step 3: Write the minimal `MixAdvicePluginAdapter.h`**

```cpp
#pragma once
#include "DistrhoPlugin.hpp"

START_NAMESPACE_DISTRHO

enum Parameters { kParameterPresetIndex, kParameterCount };

class MixAdvicePluginAdapter : public Plugin
{
public:
    MixAdvicePluginAdapter();

protected:
    const char* getLabel() const override { return "MixAdvice"; }
    const char* getDescription() const override { return "Realtime mix analyzer and pre-mastering advisor"; }
    const char* getMaker() const override { return "Spellbound"; }
    const char* getLicense() const override { return "https://spellbound.audio/plugins/mixadvice#license"; }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }

    void initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

    void run(const float** inputs, float** outputs, uint32_t frames) override;

public:
    int getNumPresets() const noexcept;
    const char* getPresetName(int index) const noexcept;

private:
    float presetIndex = 0.f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAdvicePluginAdapter)
};

END_NAMESPACE_DISTRHO
```

- [ ] **Step 4: Write the minimal `MixAdvicePluginAdapter.cpp`** (passthrough only — no `AnalyserEngine`/`PresetManager` wiring yet, that's Task 6)

```cpp
#include "MixAdvicePluginAdapter.h"
#include <cstring>

START_NAMESPACE_DISTRHO

MixAdvicePluginAdapter::MixAdvicePluginAdapter()
    : Plugin(kParameterCount, 0, 0)
{
}

void MixAdvicePluginAdapter::initParameter(const uint32_t index, Parameter& parameter)
{
    if (index != kParameterPresetIndex) return;
    parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
    parameter.name   = "Preset";
    parameter.symbol = "preset";
    parameter.ranges.def = 0.f;
    parameter.ranges.min = 0.f;
    parameter.ranges.max = 0.f;   // Task 6 sets this to (numPresets - 1) once PresetManager is wired in
}

float MixAdvicePluginAdapter::getParameterValue(const uint32_t index) const
{
    return index == kParameterPresetIndex ? presetIndex : 0.f;
}

void MixAdvicePluginAdapter::setParameterValue(const uint32_t index, const float value)
{
    if (index == kParameterPresetIndex) presetIndex = value;
}

void MixAdvicePluginAdapter::run(const float** inputs, float** outputs, uint32_t frames)
{
    if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
    if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
}

int MixAdvicePluginAdapter::getNumPresets() const noexcept { return 0; }
const char* MixAdvicePluginAdapter::getPresetName(int) const noexcept { return ""; }

Plugin* createPlugin() { return new MixAdvicePluginAdapter(); }

END_NAMESPACE_DISTRHO
```

- [ ] **Step 5: Rewrite `CMakeLists.txt`'s dependency section**

```cmake
cmake_minimum_required(VERSION 3.22)
project(MixAdvice VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(AUDIOPLUGINS_DPF_GIT_TAG "4238e1c7f0351bbe488d79f0899c540543ac7583" CACHE STRING "Pinned DPF commit")

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

set(MIXADVICE_DPF_TARGETS vst3 clap lv2)
if(APPLE)
    list(APPEND MIXADVICE_DPF_TARGETS au)
endif()

dpf_add_plugin(MixAdvice
    TARGETS ${MIXADVICE_DPF_TARGETS}
    UI_TYPE opengl
    FILES_DSP
        Source/MixAdvicePluginAdapter.cpp
        Source/Analysis/AnalyserEngine.cpp
        Source/Analysis/ResonanceWorker.cpp
        Source/Analysis/ResonancePeakMath.cpp
        Source/Analysis/AdviceAdapter.cpp
        Source/Presets/PresetManager.cpp
        ${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.cpp
    FILES_UI
        Source/MixAdviceUI.cpp
        Source/UI/MasteringAdvicePanel.cpp
)

target_include_directories(MixAdvice PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source" "${CMAKE_BINARY_DIR}/generated")

# Same DPF/GCC WebView-leak-workaround extensions requirement as Hex/Pugilist.
if(TARGET dgl-opengl)
    set_target_properties(dgl-opengl PROPERTIES CXX_EXTENSIONS ON)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(AudioPluginsCommon
    GIT_REPOSITORY https://github.com/TriYop/spellbound-common.git
    GIT_TAG        v0.8.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(AudioPluginsCommon)

target_link_libraries(MixAdvice PUBLIC AudioPluginsCommon::dsp AudioPluginsCommon::analysis AudioPluginsCommon::io)
target_link_libraries(MixAdvice-ui PRIVATE AudioPluginsCommon::hui_dgl AudioPluginsCommon::presets)

# (the CMake preset-embedding block from Task 4 Step 3 stays here, unchanged,
# placed before dpf_add_plugin() since EmbeddedPresets.cpp is one of its
# FILES_DSP sources above)
```

(Remove the old `enable_testing()` / `add_subdirectory(Tests)` / `juce_add_plugin` / `clap_juce_extensions_plugin` blocks entirely — Task 9's CI step calls `ctest` against a build that still has `enable_testing()`/`add_subdirectory(Tests)` re-added right after the `FetchContent_MakeAvailable(AudioPluginsCommon)` line, same position Hex's own `CMakeLists.txt` doesn't need since Hex's tests are flat `add_executable`s in the top-level file, not a subdirectory — TrueSight keeps its own `Tests/CMakeLists.txt` subdirectory convention from Stage 4A, so re-add `enable_testing()` and `add_subdirectory(Tests)` here.)

- [ ] **Step 6: Configure, build**

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
```

Expected: `build/bin/MixAdvice.vst3`, `build/bin/MixAdvice.clap`, `build/bin/MixAdvice.lv2` all produced; `ctest --test-dir build` still runs all Tasks 2-4 tests green.

- [ ] **Step 7: Commit**

```bash
git add Source/DistrhoPluginInfo.h Source/MixAdvicePluginAdapter.h Source/MixAdvicePluginAdapter.cpp \
        Source/_juce_reference CMakeLists.txt cmake/
git commit -m "feat: bootstrap DPF build (passthrough MixAdvicePluginAdapter, no JUCE)"
```

---

### Task 6: Wire the analysis pipeline into `MixAdvicePluginAdapter`

**Repo: TrueSight**

**Files:**
- Modify: `Source/MixAdvicePluginAdapter.h`, `Source/MixAdvicePluginAdapter.cpp`

**Interfaces:**
- Consumes: `AnalyserEngine` (Task 2), `PresetManager` (Task 4) — both already framework-free.
- Produces: `const AnalysisResult& getAnalysisResult() const noexcept`, `const PresetManager& getPresetManager() const noexcept`, `bool isCurrentlyPlaying() const noexcept` — the exact direct-access surface `MixAdviceUI` (Tasks 7-8) reads via `getPluginInstancePointer()`, following Hex's `getInputLevel()`/`getOutputLevel()` precedent (public methods below the `protected:` DPF overrides, same class).

- [ ] **Step 1: Extend the header**

```cpp
// Source/MixAdvicePluginAdapter.h
#pragma once
#include "DistrhoPlugin.hpp"
#include "Analysis/AnalyserEngine.h"
#include "Presets/PresetManager.h"
#include <atomic>

START_NAMESPACE_DISTRHO

enum Parameters { kParameterPresetIndex, kParameterCount };

class MixAdvicePluginAdapter : public Plugin
{
public:
    MixAdvicePluginAdapter();

protected:
    const char* getLabel() const override { return "MixAdvice"; }
    const char* getDescription() const override { return "Realtime mix analyzer and pre-mastering advisor"; }
    const char* getMaker() const override { return "Spellbound"; }
    const char* getLicense() const override { return "https://spellbound.audio/plugins/mixadvice#license"; }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }

    void initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

    void activate() override;
    void deactivate() override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

public:
    const AnalysisResult& getAnalysisResult() const noexcept { return analyser_.result; }
    const PresetManager& getPresetManager() const noexcept { return presetManager_; }
    bool isCurrentlyPlaying() const noexcept { return isPlaying_.load(std::memory_order_relaxed); }

private:
    PresetManager presetManager_;
    int presetIndex_ = 0;
    bool wasPlaying_ = false;
    std::atomic<bool> isPlaying_ { false };
    AnalyserEngine analyser_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAdvicePluginAdapter)
};

END_NAMESPACE_DISTRHO
```

- [ ] **Step 2: Rewrite the implementation**

```cpp
#include "MixAdvicePluginAdapter.h"

START_NAMESPACE_DISTRHO

MixAdvicePluginAdapter::MixAdvicePluginAdapter()
    : Plugin(kParameterCount, 0, 0)
{
}

void MixAdvicePluginAdapter::initParameter(const uint32_t index, Parameter& parameter)
{
    if (index != kParameterPresetIndex) return;
    parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
    parameter.name   = "Preset";
    parameter.symbol = "preset";
    parameter.ranges.def = 0.f;
    parameter.ranges.min = 0.f;
    parameter.ranges.max = static_cast<float>(std::max(0, presetManager_.getNumPresets() - 1));
}

float MixAdvicePluginAdapter::getParameterValue(const uint32_t index) const
{
    return index == kParameterPresetIndex ? static_cast<float>(presetIndex_) : 0.f;
}

void MixAdvicePluginAdapter::setParameterValue(const uint32_t index, const float value)
{
    if (index != kParameterPresetIndex) return;
    const int maxIndex = std::max(0, presetManager_.getNumPresets() - 1);
    presetIndex_ = std::clamp(static_cast<int>(value + 0.5f), 0, maxIndex);
}

void MixAdvicePluginAdapter::activate()
{
    analyser_.prepare(getSampleRate(), static_cast<int>(getBufferSize()), 2);
    analyser_.resetPeaks();
    wasPlaying_ = false;
}

void MixAdvicePluginAdapter::deactivate()
{
    analyser_.suspend();
}

void MixAdvicePluginAdapter::run(const float** inputs, float** outputs, uint32_t frames)
{
    if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
    if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);

    // DPF has no JUCE-style getPlayHead()/PositionInfo abstraction exposed to
    // Plugin directly for transport play-state in every format uniformly;
    // gate on signal level exactly like TrueSight's pre-migration Standalone
    // path did (see Source/_juce_reference/PluginProcessor.cpp's "Standalone
    // (no playhead)" branch) -- simpler and format-uniform, at the cost of
    // losing the DAW-transport-exact gating the JUCE version had when a host
    // playhead was available. Revisit if DPF exposes transport state later.
    double sumSq = 0.0;
    for (uint32_t i = 0; i < frames; ++i)
        sumSq += static_cast<double>(outputs[0][i]) * outputs[0][i];
    const float rms = static_cast<float>(std::sqrt(sumSq / std::max<uint32_t>(1, frames)));
    const bool shouldProcess = rms > 1e-4f;

    isPlaying_.store(shouldProcess, std::memory_order_relaxed);
    if (shouldProcess && !wasPlaying_)
        analyser_.resetPeaks();
    wasPlaying_ = shouldProcess;

    if (shouldProcess)
        analyser_.process(outputs, 2, static_cast<int>(frames));
}

Plugin* createPlugin() { return new MixAdvicePluginAdapter(); }

END_NAMESPACE_DISTRHO
```

Note the documented behavior change in the `run()` comment: this drops the exact DAW-transport-following gate the JUCE version had (`getPlayHead()->getPosition()->getIsPlaying()`) in favor of the signal-level gate the old Standalone path already used, since DPF's `Plugin` base doesn't expose a uniform playhead query across all three formats the way JUCE's `AudioProcessor` did. This is a real, intentional scope decision for this plan, not an oversight — flag it in the manual verification pass (Task 11).

- [ ] **Step 3: Update `initParameter`'s max range after `PresetManager` construction order**

`presetManager_` is a member initialized before `initParameter()` is ever called (DPF constructs the `Plugin` fully, including all members, before calling `initParameter()` once per parameter) — no ordering fix needed, confirm this holds by inspecting DPF's `Plugin` construction sequence in `dpf-src/distrho/DistrhoPlugin.hpp` if this assumption needs verifying at implementation time.

- [ ] **Step 4: Build and smoke-test manually**

```bash
cmake --build build --parallel
xvfb-run -a build/bin/MixAdvice.clap   # won't "run" standalone (no Standalone target) -- use a host or clap-validator, see Task 9
```

There is no automated test for this task specifically — DPF `Plugin` subclasses are validated via the format validators in CI (Task 9), matching Hex's own precedent (Hex has zero DPF-adapter-level unit tests either). Confirm this task's correctness by re-running the full `ctest` suite (still green — nothing in Tasks 2-4's tests changed) and a local `pluginval`/`clap-validator` pass if either tool is already installed locally (both are fetched fresh in Task 9's CI if not).

- [ ] **Step 5: Commit**

```bash
git add Source/MixAdvicePluginAdapter.h Source/MixAdvicePluginAdapter.cpp
git commit -m "feat: wire AnalyserEngine + PresetManager into MixAdvicePluginAdapter"
```

---

### Task 7: `MixAdviceUI` core — spectrum meters, correlation gauges, preset selector

**Repo: TrueSight**

**Files:**
- Create: `Source/MixAdviceUI.h`, `Source/MixAdviceUI.cpp`

**Interfaces:**
- Consumes: `common::hui::dgl::SpectrumMeter` (`setLevels(left[7], right[7])`, `setReferences(refs[7])`), `common::hui::dgl::CorrelationGauge` (`setValue(float)`), `common::hui::dgl::PresetSelector` (`setEntries(vector<PresetEntry>)`, `setCurrentIndex(int)`, `onIndexSelected`), `common::hui::dgl::AdviceLabel` (`setText`, `setCategory`) — all from `v0.8.0`. `MixAdvicePluginAdapter::getAnalysisResult()`/`getPresetManager()`/`isCurrentlyPlaying()` (Task 6), via `getPluginInstancePointer()` (Hex's `fPluginPtr` pattern).
- Produces: the `parameterChanged`/`uiIdle` polling loop and preset-switch wiring Task 8 extends with the advice/resonance panel — `MixAdviceUI` itself does not yet construct `MasteringAdvicePanel` (that's Task 8's addition to this same file).

- [ ] **Step 1: Write `MixAdviceUI.h`**

```cpp
#pragma once
#include "DistrhoUI.hpp"
#include "MixAdvicePluginAdapter.h"
#include "Analysis/BandConfig.h"
#include "audioplugins/common/hui/dgl/SpectrumMeter.h"
#include "audioplugins/common/hui/dgl/CorrelationGauge.h"
#include "audioplugins/common/hui/dgl/PresetSelector.h"
#include "audioplugins/common/hui/dgl/AdviceLabel.h"
#include <array>
#include <memory>

START_NAMESPACE_DISTRHO

class MixAdviceUI : public UI
{
public:
    MixAdviceUI();

protected:
    void parameterChanged(uint32_t index, float value) override;
    void uiIdle() override;
    void onNanoDisplay() override;

private:
    void refreshPresetSelector();

    MixAdvicePluginAdapter* const fPluginPtr;

    std::unique_ptr<audioplugins::common::hui::dgl::SpectrumMeter> fSpectrumMeter;
    std::array<std::unique_ptr<audioplugins::common::hui::dgl::CorrelationGauge>, BandConfig::numBands> fCorrelationGauges;
    std::unique_ptr<audioplugins::common::hui::dgl::PresetSelector> fPresetSelector;
    std::unique_ptr<audioplugins::common::hui::dgl::AdviceLabel> fWarmupLabel;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixAdviceUI)
};

END_NAMESPACE_DISTRHO
```

- [ ] **Step 2: Write `MixAdviceUI.cpp`**

```cpp
#include "MixAdviceUI.h"
#include "audioplugins/common/presets/PresetBrowser.h"   // for PresetEntry only

START_NAMESPACE_DISTRHO

using audioplugins::common::hui::dgl::SpectrumMeter;
using audioplugins::common::hui::dgl::CorrelationGauge;
using audioplugins::common::hui::dgl::PresetSelector;
using audioplugins::common::hui::dgl::AdviceLabel;
using audioplugins::common::hui::dgl::AdviceCategory;
using audioplugins::common::presets::PresetEntry;

static constexpr uint kWindowWidth  = 760;
static constexpr uint kWindowHeight = 520;

MixAdviceUI::MixAdviceUI()
    : UI(kWindowWidth, kWindowHeight)
    , fPluginPtr(static_cast<MixAdvicePluginAdapter*>(getPluginInstancePointer()))
{
    fSpectrumMeter = std::make_unique<SpectrumMeter>(this);
    fSpectrumMeter->setAbsolutePos(20, 60);
    fSpectrumMeter->setSize(720, 240);

    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        fCorrelationGauges[static_cast<size_t>(i)] = std::make_unique<CorrelationGauge>(this);
        fCorrelationGauges[static_cast<size_t>(i)]->setAbsolutePos(20 + i * 100, 320);
        fCorrelationGauges[static_cast<size_t>(i)]->setSize(90, 70);
    }

    fPresetSelector = std::make_unique<PresetSelector>(this);
    fPresetSelector->setAbsolutePos(20, 12);
    fPresetSelector->setClosedSize(300, 28);
    fPresetSelector->onIndexSelected = [this](int index)
    {
        fPluginPtr->setParameterValue(kParameterPresetIndex, static_cast<float>(index));
        setParameterValue(kParameterPresetIndex, static_cast<float>(index));   // notify host
    };

    fWarmupLabel = std::make_unique<AdviceLabel>(this);
    fWarmupLabel->setAbsolutePos(20, 400);
    fWarmupLabel->setSize(720, 28);
    fWarmupLabel->setCategory(AdviceCategory::WARNING);
    fWarmupLabel->setText("Play audio to compute mastering recommendations");

    refreshPresetSelector();
}

void MixAdviceUI::refreshPresetSelector()
{
    const auto& mgr = fPluginPtr->getPresetManager();
    std::vector<PresetEntry> entries;
    entries.reserve(static_cast<size_t>(mgr.getNumPresets()));
    for (int i = 0; i < mgr.getNumPresets(); ++i)
        entries.push_back({ mgr.getPreset(i).name, /*isFactory=*/true });   // no user Save/Delete -- see plan's Deviation 3
    fPresetSelector->setEntries(std::move(entries));
    fPresetSelector->setCurrentIndex(static_cast<int>(fPluginPtr->getParameterValue(kParameterPresetIndex)));
}

void MixAdviceUI::parameterChanged(const uint32_t index, const float value)
{
    if (index == kParameterPresetIndex)
        fPresetSelector->setCurrentIndex(static_cast<int>(value));
}

void MixAdviceUI::uiIdle()
{
    const auto snap = fPluginPtr->getAnalysisResult().read();

    std::array<float, BandConfig::numBands> levelsL{}, levelsR{}, refs{};
    const auto& preset = fPluginPtr->getPresetManager().getPreset(
        static_cast<int>(fPluginPtr->getParameterValue(kParameterPresetIndex)));

    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        // Map dBFS (BandConfig::displayFloorDb..displayCeilDb) to 0..1 for the meter.
        const float floor = BandConfig::displayFloorDb, ceil = BandConfig::displayCeilDb;
        levelsL[static_cast<size_t>(i)] = std::clamp((snap.rmsDbL[static_cast<size_t>(i)] - floor) / (ceil - floor), 0.f, 1.f);
        levelsR[static_cast<size_t>(i)] = std::clamp((snap.rmsDbR[static_cast<size_t>(i)] - floor) / (ceil - floor), 0.f, 1.f);
        refs[static_cast<size_t>(i)]    = std::clamp((preset.bandRmsDb[static_cast<size_t>(i)] - floor) / (ceil - floor), 0.f, 1.f);

        fCorrelationGauges[static_cast<size_t>(i)]->setValue(
            (snap.correlation[static_cast<size_t>(i)] + 1.f) * 0.5f);   // correlation is -1..1, gauge wants 0..1
    }
    fSpectrumMeter->setLevels(levelsL.data(), levelsR.data());
    fSpectrumMeter->setReferences(refs.data());

    const float overallMax = (snap.peakOverallDbL + snap.peakOverallDbR) * 0.5f;
    fWarmupLabel->setVisible(overallMax <= -99.f);
}

void MixAdviceUI::onNanoDisplay() {}

UI* createUI() { return new MixAdviceUI(); }

END_NAMESPACE_DISTRHO
```

Layout coordinates (`kWindowWidth`/`kWindowHeight`, widget positions) are placeholder-but-real values, not `TBD` — the implementer adjusts them visually during Task 11's manual verification pass; they compile and produce a usable (if inelegant) layout as written.

- [ ] **Step 3: Build**

```bash
cmake --build build --parallel
```

Expected: `MixAdvice-ui` target compiles and links against `AudioPluginsCommon::hui_dgl`/`AudioPluginsCommon::presets`.

- [ ] **Step 4: Commit**

```bash
git add Source/MixAdviceUI.h Source/MixAdviceUI.cpp
git commit -m "feat: MixAdviceUI core -- spectrum meter, correlation gauges, preset selector"
```

---

### Task 8: Mastering-advice + resonance panel

**Repo: TrueSight**

**Files:**
- Create: `Source/UI/MasteringAdvicePanel.h`, `Source/UI/MasteringAdvicePanel.cpp`
- Modify: `Source/MixAdviceUI.h`, `Source/MixAdviceUI.cpp` (construct and feed the panel)

**Interfaces:**
- Consumes: `audioplugins::common::analysis::deriveAdvice(AnalysisSnapshot, PresetData) -> AdviceSet` (existing), `buildAnalysisSnapshot(AnalysisResult::Snapshot, float warmupSec) -> AnalysisSnapshot` and `buildResonancePeaks(AnalysisResult::Snapshot) -> vector<ResonancePeak>` (both already in `Source/Analysis/AdviceAdapter.h`, unchanged by this plan).
- Produces: `class MasteringAdvicePanel : public DGL_NAMESPACE::NanoSubWidget` with `void update(const AdviceSet&, const std::vector<ResonancePeak>&, float lraLu)`, called from `MixAdviceUI::uiIdle()`.

This is a NanoVG port of `Source/_juce_reference/PluginEditor.cpp:551`'s `drawAdvicePanel()` (per this plan's Deviation 4) — same data, same per-band EQ-gain/Q text and mixbus-comp/loudness rows, redrawn with `beginPath()/fillColor()/text()` (the pattern confirmed in `Common/src/hui/dgl/{SpectrumMeter,AdviceLabel}.cpp`) instead of `juce::Graphics`. The per-band center-out gain bar and the resonance-peak list are real but simplified relative to the original pixel-for-pixel JUCE layout — visual polish is a Task 11 manual-pass follow-up, not this task's gate.

- [ ] **Step 1: Write `MasteringAdvicePanel.h`**

```cpp
#pragma once
#include "NanoVG.hpp"
#include "audioplugins/common/hui/Theme.h"
#include "audioplugins/common/analysis/AdviceSet.h"
#include "audioplugins/common/analysis/ResonancePeakPicker.h"
#include <vector>

class MasteringAdvicePanel : public DGL_NAMESPACE::NanoSubWidget
{
public:
    explicit MasteringAdvicePanel(DGL_NAMESPACE::NanoTopLevelWidget* parent);

    void update(const audioplugins::common::analysis::AdviceSet& advice,
                const std::vector<audioplugins::common::analysis::ResonancePeak>& resonances,
                float lraLu);

protected:
    void onNanoDisplay() override;

private:
    audioplugins::common::analysis::AdviceSet advice_;
    std::vector<audioplugins::common::analysis::ResonancePeak> resonances_;
    float lraLu_ = 0.f;

    DISTRHO_LEAK_DETECTOR(MasteringAdvicePanel)
};
```

- [ ] **Step 2: Write `MasteringAdvicePanel.cpp`**

```cpp
#include "MasteringAdvicePanel.h"
#include "Analysis/BandConfig.h"
#include <cstdio>

MasteringAdvicePanel::MasteringAdvicePanel(DGL_NAMESPACE::NanoTopLevelWidget* const parent)
    : DGL_NAMESPACE::NanoSubWidget(parent)
{
}

void MasteringAdvicePanel::update(const audioplugins::common::analysis::AdviceSet& advice,
                                   const std::vector<audioplugins::common::analysis::ResonancePeak>& resonances,
                                   const float lraLu)
{
    advice_ = advice;
    resonances_ = resonances;
    lraLu_ = lraLu;
    repaint();
}

void MasteringAdvicePanel::onNanoDisplay()
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    if (w <= 0.f || h <= 0.f) return;

    beginPath();
    rect(0.f, 0.f, w, h);
    fillColor(DGL_NAMESPACE::Color(0x13, 0x13, 0x1f, 1.0f));
    fill();
    closePath();

    fontSize(9.0f);
    textAlign(ALIGN_LEFT | ALIGN_TOP);
    fillColor(DGL_NAMESPACE::Color(0xcc, 0xcc, 0xcc, 1.0f));

    const float slotW = w / static_cast<float>(BandConfig::numBands);
    char line[64];

    for (size_t i = 0; i < static_cast<size_t>(BandConfig::numBands); ++i)
    {
        const auto& eq = advice_.eq[i];
        const float x = static_cast<float>(i) * slotW + 4.f;

        std::snprintf(line, sizeof(line), "%.0fHz", BandConfig::bandCenterHz[i]);
        beginPath(); text(x, 4.f, line, nullptr); closePath();

        std::snprintf(line, sizeof(line), eq.gainDb == 0.f ? "flat" : "%+.1fdB", eq.gainDb);
        beginPath();
        fillColor(eq.gainDb == 0.f ? DGL_NAMESPACE::Color(0xaa, 0xaa, 0xaa, 1.0f)
                                    : eq.gainDb > 0.f ? DGL_NAMESPACE::Color(0x55, 0xdd, 0x77, 1.0f)
                                                       : DGL_NAMESPACE::Color(0xff, 0xaa, 0x44, 1.0f));
        text(x, 18.f, line, nullptr);
        closePath();
    }

    const auto& mb = advice_.mixbusComp;
    std::snprintf(line, sizeof(line), "Mixbus: T:%.0f  %.1f:1  A:%.0fms R:%.0fms  Mkp:+%.1fdB",
                  mb.thresholdDb, mb.ratio, mb.attackMs, mb.releaseMs, mb.makeupDb);
    beginPath(); fillColor(DGL_NAMESPACE::Color(0xcc, 0xcc, 0xcc, 1.0f)); text(4.f, 36.f, line, nullptr); closePath();

    std::snprintf(line, sizeof(line), "LRA: %s   Limiter target: %.1f LUFS   Ceiling: -1.0 dBTP",
                  lraLu_ > 0.f ? std::to_string(lraLu_).c_str() : "--", advice_.limiter.targetLufsApprox);
    beginPath(); text(4.f, 50.f, line, nullptr); closePath();

    float ry = 68.f;
    for (const auto& peak : resonances_)
    {
        std::snprintf(line, sizeof(line), "Cut: %.0fHz  Q:%.1f  %.1fdB", peak.freqHz, peak.q, peak.gainDb);
        beginPath(); text(4.f, ry, line, nullptr); closePath();
        ry += 12.f;
    }
}
```

- [ ] **Step 3: Wire it into `MixAdviceUI`**

```cpp
// Source/MixAdviceUI.h — add member
#include "UI/MasteringAdvicePanel.h"
#include "Analysis/AdviceAdapter.h"
/* ... */
std::unique_ptr<MasteringAdvicePanel> fAdvicePanel;
```

```cpp
// Source/MixAdviceUI.cpp — constructor, after fWarmupLabel setup
fAdvicePanel = std::make_unique<MasteringAdvicePanel>(this);
fAdvicePanel->setAbsolutePos(20, 440);
fAdvicePanel->setSize(720, 70);
```

```cpp
// uiIdle(), after the warmup-label visibility check
if (overallMax > -99.f)
{
    const auto commonSnap = buildAnalysisSnapshot(snap, /*warmupSec=*/5.0f);
    const auto advice = audioplugins::common::analysis::deriveAdvice(commonSnap, /* PresetData -> analysis::PresetData */ preset);
    auto resonances = buildResonancePeaks(snap);
    fAdvicePanel->setVisible(true);
    fAdvicePanel->update(advice, resonances, snap.lraLu);
}
else
{
    fAdvicePanel->setVisible(false);
}
```

(`buildAnalysisSnapshot`'s second argument was `kPercentileWarmupSec` in the JUCE-era `PluginEditor.cpp` — that constant's actual value must be carried over from `Source/_juce_reference/PluginEditor.cpp` rather than the placeholder `5.0f` above; check that file for the real value before finalizing this step.)

- [ ] **Step 4: Build**

```bash
cmake --build build --parallel
```

- [ ] **Step 5: Commit**

```bash
git add Source/UI/MasteringAdvicePanel.h Source/UI/MasteringAdvicePanel.cpp \
        Source/MixAdviceUI.h Source/MixAdviceUI.cpp
git commit -m "feat: mastering-advice and resonance panel (NanoVG port of drawAdvicePanel)"
```

---

### Task 9: Linux CI workflow

**Repo: TrueSight**

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `secrets.COMMON_REPO_TOKEN` (already provisioned at the GitHub org/repo level for every other migrated plugin — confirm it's set on `spellbound-TrueSight` too, not just Hex/Pugilist/etc; if absent, this is a one-time manual step outside this plan's scope, same as every prior plugin's migration).

- [ ] **Step 1: Copy Hex's workflow and adapt the plugin-specific bits**

```yaml
name: CI

on:
  push:
    branches: [ "main", "master" ]
    tags: [ "v*" ]
  pull_request:
    branches: [ "main", "master" ]

jobs:
  build-and-test:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
        build_type: [Release]

    runs-on: ${{ matrix.os }}
    continue-on-error: ${{ matrix.os != 'ubuntu-latest' }}

    steps:
      - uses: actions/checkout@v4

      - name: Install Linux build dependencies
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libasound2-dev libjack-jackd2-dev \
            libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
            libxinerama-dev libxrandr-dev libxrender-dev \
            libfreetype-dev libfontconfig1-dev \
            libglu1-mesa-dev libwebkit2gtk-4.1-dev \
            xvfb

      - name: Configure Common repo access
        run: git config --global url."https://x-access-token:${{ secrets.COMMON_REPO_TOKEN }}@github.com/TriYop/spellbound-common".insteadOf "https://github.com/TriYop/spellbound-common"

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}

      - name: Build
        run: cmake --build build --config ${{ matrix.build_type }} --parallel

      - name: Test
        working-directory: build
        run: ctest --build-config ${{ matrix.build_type }} --output-on-failure

      - name: Download pluginval
        if: runner.os == 'Linux'
        run: |
          curl -sL -o pluginval_Linux.zip \
            https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip
          unzip -o pluginval_Linux.zip
          chmod +x pluginval

      - name: Validate VST3 with pluginval
        if: runner.os == 'Linux'
        run: xvfb-run -a ./pluginval --strictness-level 5 --validate build/bin/MixAdvice.vst3

      - name: Download clap-validator
        if: runner.os == 'Linux'
        run: |
          curl -sL -o clap-validator.zip \
            https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip
          unzip -o clap-validator.zip
          tar -xzf clap-validator-*-ubuntu-22.04.tar.gz
          chmod +x clap-validator

      - name: Validate CLAP with clap-validator
        if: runner.os == 'Linux'
        run: xvfb-run -a ./clap-validator validate build/bin/MixAdvice.clap

      - name: Install lv2lint build dependencies
        if: runner.os == 'Linux'
        run: sudo apt-get install -y liblilv-dev lv2-dev libelf-dev meson ninja-build

      - name: Build lv2lint
        if: runner.os == 'Linux'
        run: |
          git clone --depth 1 https://github.com/sfztools/lv2lint /tmp/lv2lint
          meson setup -Donline-tests=disabled -Delf-tests=enabled -Dx11-tests=disabled /tmp/lv2lint/build /tmp/lv2lint
          ninja -C /tmp/lv2lint/build

      - name: Validate LV2 with lv2lint
        if: runner.os == 'Linux'
        continue-on-error: true   # same DPF-wide doap:Project finding as every other DPF plugin here -- see Hex's ci.yml comment
        run: |
          export LV2_PATH="/usr/lib/lv2:${GITHUB_WORKSPACE}/build/bin"
          export LD_PRELOAD="/tmp/lv2lint/build/lv2lint.so"
          /tmp/lv2lint/build/lv2lint.bin -s lv2_generate_ttl "https://spellbound.audio/plugins/mixadvice"
```

Two things to verify at implementation time rather than assume from Hex's file verbatim: (a) whether TrueSight's Stage 4A `Tests/` CTest suite needs any additional apt packages Hex's workflow doesn't install (it currently doesn't — `pugixml` is FetchContent'd by Common itself, no system package needed), and (b) whether `pluginval`/`clap-validator` need `xvfb-run` for TrueSight specifically given its heavier NanoVG UI (`SpectrumMeter` + 7 `CorrelationGauge`s + `MasteringAdvicePanel`) — keep `xvfb-run` (already in Hex's file) since both validators load-and-close the UI headlessly regardless of its complexity.

- [ ] **Step 2: Push and watch the first run**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add Linux-gated GitHub Actions workflow (build, test, pluginval, clap-validator, lv2lint)"
git push
gh run watch   # or: gh run list, then gh run view <id> --log
```

Fix whatever the first real run finds (following Hex's own history — its first real CI run found 4 genuine bugs, not zero) rather than assuming success.

---

### Task 10: Packaging (CPack tarball)

**Repo: TrueSight**

**Files:**
- Modify: `CMakeLists.txt` (append install rules + CPack section, following Hex's exact block)

**Interfaces:**
- Consumes: nothing new.
- Produces: `build-release/MixAdvice-<version>-linux-x86_64.tar.gz` + `.sha256` on `cpack`.

- [ ] **Step 1: Append install + CPack rules**

```cmake
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(_BIN "${CMAKE_BINARY_DIR}/bin")

install(DIRECTORY  "${_BIN}/MixAdvice.vst3"
        DESTINATION VST3
        COMPONENT   Runtime
        USE_SOURCE_PERMISSIONS)

if(APPLE)
    install(DIRECTORY  "${_BIN}/MixAdvice.clap"
            DESTINATION CLAP
            COMPONENT   Runtime
            USE_SOURCE_PERMISSIONS)
else()
    install(FILES      "${_BIN}/MixAdvice.clap"
            DESTINATION CLAP
            COMPONENT   Runtime
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

install(DIRECTORY  "${_BIN}/MixAdvice.lv2"
        DESTINATION LV2
        COMPONENT   Runtime
        USE_SOURCE_PERMISSIONS)

install(FILES      ${MIXADVICE_PRESET_FILES}
        DESTINATION Presets
        COMPONENT   Runtime)

install(PROGRAMS   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install.sh"
                   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/uninstall.sh"
        DESTINATION .
        COMPONENT   Runtime)

set(CPACK_GENERATOR                 TGZ)
set(CPACK_PACKAGE_NAME              MixAdvice)
set(CPACK_PACKAGE_VENDOR            Spellbound)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Realtime mix analyzer and pre-mastering advisor")
set(CPACK_PACKAGE_VERSION           ${PROJECT_VERSION})
set(CPACK_SYSTEM_NAME               linux-x86_64)
set(CPACK_PACKAGE_FILE_NAME         "MixAdvice-${PROJECT_VERSION}-linux-x86_64")
set(CPACK_PACKAGING_INSTALL_PREFIX  "")
set(CPACK_STRIP_FILES               TRUE)
set(CPACK_PACKAGE_CHECKSUM          SHA256)
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")
include(CPack)
```

Confirm `scripts/install.sh`/`scripts/uninstall.sh` (already referenced by the pre-migration `CMakeLists.txt`) still work unmodified against the new DPF `bin/VST3|CLAP|LV2` layout instead of JUCE's `<Target>_artefacts/<Config>/<Format>` layout — inspect both scripts and update their hardcoded source paths if they assumed the JUCE layout.

- [ ] **Step 2: Build and verify the tarball**

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
cd build-release && cpack
tar -tzf MixAdvice-0.2.0-linux-x86_64.tar.gz   # sanity-check contents
```

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add CPack tarball packaging for the DPF build"
```

---

### Task 11: Manual verification checklist

**Repo: TrueSight**

**Files:**
- Create: `docs/manual-verification-stage4b.md`

- [ ] **Step 1: Write the checklist**

```markdown
# Stage 4B Manual Verification Checklist

Load `build/bin/MixAdvice.vst3` (or `.clap`) in a real Linux host (Reaper, Bitwig, or DPF's own `jalv` for LV2) and confirm:

- [ ] Audio passes through unmodified (A/B against a bypassed track -- MixAdvice is a pure analyzer, never alters the signal).
- [ ] Spectrum meter bars update in real time as audio plays; reference lines reflect the currently selected preset's `bandRmsDb` targets.
- [ ] Correlation gauges move toward green for in-phase (mono-compatible) material and red for out-of-phase test material (e.g. an inverted-R test file).
- [ ] Preset selector lists every factory preset (compare count/names against `Presets/*.xml`) plus any preset dropped into `~/.config/MixAdvice/Presets/`; selecting one updates the reference lines and mastering-advice panel.
- [ ] Mastering-advice panel shows non-zero EQ/comp numbers once audio has played past the warm-up window (5s -- confirm against the constant carried over in Task 8 Step 3), and the "Play audio..." warning disappears once it does.
- [ ] Resonance-cut list updates every few seconds while a resonant test tone plays, and clears/resets when playback restarts (host stop/rewind/play).
- [ ] Plugin survives host pause/resume/seek without crashing or hanging (exercises `ResonanceWorker`'s queue-backlog-drains-gracefully design from Task 3).
- [ ] Preset selection persists across a project save/reload (confirms the automatable `preset` parameter's default host-side state save covers what the old JUCE `getStateInformation`/`setStateInformation` used to do explicitly -- see this plan's Deviation 1).
- [ ] Note whether the dropped DAW-transport-exact play/stop gating (Task 6 Step 2's documented behavior change, signal-RMS gate instead of `getPlayHead()`) is noticeable in practice (e.g. very quiet passages briefly toggling the gate) -- file a follow-up issue if so, don't block Stage 4B on it.

Record the outcome (pass/fail per line, plus host name/version used) in this file and commit it once verification is complete -- this is the "manual verification pass" that Stage 4B's exit criteria (roadmap `Verification approach` section) require documented, not just performed.
```

- [ ] **Step 2: Commit**

```bash
git add docs/manual-verification-stage4b.md
git commit -m "docs: add Stage 4B manual verification checklist"
```

Perform the checklist, fill in results, commit the filled-in version as a follow-up (not blocking the rest of this plan's tasks, per the roadmap's "not gating" note on manual verification — but required before Stage 4B is declared closed in the roadmap doc).
