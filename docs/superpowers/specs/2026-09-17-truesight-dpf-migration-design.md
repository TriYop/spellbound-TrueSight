# Stage 4B: TrueSight DPF Migration + Common Extraction

**Date:** 2026-09-17  
**Context:** Phase 4 of the AudioPlugins JUCE→DPF migration roadmap. Stage 4A extracted TrueSight's analysis/DSP layer to `Common/` (completed 2026-09-14/15). Stage 4B migrates the plugin wrapper itself: JUCE AudioProcessor/Editor → DPF Plugin, resonance worker threading, UI component extraction, CI/packaging.

**Approach:** Full extraction (Approach B from brainstorming) — migrate TrueSight to DPF while extracting resonance-worker threading and all UI components to `Common/`, validating the shared library's API before every other Phase 5+ plugin depends on it.

---

## 1. Framework Migration: JUCE → DPF

### 1.1 DPF Plugin Architecture

TrueSight replaces JUCE's `AudioProcessor`/`AudioProcessorEditor` with DPF's `Plugin` and `UI` classes:

- **`Plugin` (audio processing):**
  - `processBlock()` equivalent: `run(const float* const* inputs, float* const* outputs, uint32_t frames, const MidiEvent* events, uint32_t eventCount)`
  - Parameter tree: flat `Parameter[]` array (no hierarchical tree like JUCE's APVTS)
  - State serialization: `getState()` / `setState(const void* data, uint32_t size)` binary chunk
  - Formats: VST3 (via DPF's travesty headers, ISC-licensed), CLAP, LV2 (one codebase)

- **`UI` (GUI):**
  - Subclass DPF's `UI` base class
  - Embed DGL widgets and `common/hui` components
  - Timer callback (~30 Hz) reads atomic snapshot, repaints
  - No JUCE LookAndFeel — use `common/hui::Theme` for colors/fonts/spacing

### 1.2 File Layout

```
Source/
  PluginProcessor.h/.cpp   → Plugin.h/.cpp (DPF adapter)
  PluginEditor.h/.cpp      → UI.h/.cpp (DGL + common/hui components)
  Analysis/
    AnalysisRequest.h      — job struct: FFT magnitudes, M/S info (framework-free)
    ResonanceWorker.h/.cpp — resonance detection logic (owns common::io::AsyncWorker<>)
  Presets/
    *.xml                  — factory genre profiles (unchanged, loaded by common/presets)
Tests/
  test_plugin_*.cpp        — integration tests: parameter handling, state, preset load/save
  test_analysis_*.cpp      — unit tests for common/analysis components (framework-free)
CMakeLists.txt             — DPF configuration, Common FetchContent, preset glob
```

### 1.3 State & Parameters

**Parameter tree (DPF):**

Instead of JUCE's hierarchical APVTS, DPF uses a flat array:

```cpp
void Plugin::initParameter(uint32_t index, Parameter& p) {
  // Define ~20 parameters: preset index, genre, analysis mode, etc.
  // One Parameter per control, no nesting
}

void Plugin::setParameterValue(uint32_t index, float value) {
  // Update parameter, update currentPresetIndex if needed
}
```

**State serialization:**

```cpp
void Plugin::getState(const char*& data, uint32_t& size) {
  // Serialize current preset XML to binary blob
  // Use common::presets::PresetIO to export the active preset
  // Return pointer + size
}

void Plugin::setState(const void* data, uint32_t size) {
  // Deserialize binary blob back to preset XML
  // Call common::presets::PresetIO to import
  // Apply preset to parameters
}
```

**Preset handling:**

See Section 4 (Presets).

---

## 2. Threading: Resonance Worker + AsyncWorker

### 2.1 AsyncWorker Abstraction (common/io)

Extract the background-worker pattern to `common/io::AsyncWorker<JobType, ResultType>`:

```cpp
namespace common::io {
  template<typename JobType, typename ResultType>
  class AsyncWorker {
  public:
    AsyncWorker(size_t bufferSize = 16);
    ~AsyncWorker();  // joins thread

    // Audio thread: submit jobs (lock-free)
    bool submit(const JobType& job);

    // Audio thread: read result (lock-free, may be stale)
    ResultType getLatest() const;

    void start();
    void stop();
  private:
    std::thread worker_;
    common::io::SpscRingBuffer<JobType> queue_;
    std::atomic<ResultType> result_;
  };
}
```

This abstraction:
- Owns one `std::thread` and one `SpscRingBuffer<JobType>` (already in Common as of 4A).
- Audio thread calls `submit(job)` — non-blocking write to queue.
- Worker thread continuously reads queue, processes `JobType → ResultType`, writes result to `std::atomic<ResultType>`.
- UI thread reads `getLatest()` lock-free.
- Reusable by future plugins (SympatheticReverb, EnvironmentSim, etc.).

### 2.2 TrueSight Resonance Worker

```cpp
class ResonanceWorker {
  common::io::AsyncWorker<AnalysisRequest, AnalysisSnapshot> worker_;

  void processJob(const AnalysisRequest& req) {
    // Run resonance peak detection (common::analysis::ResonancePeakPicker)
    // Run loudness analysis (common::analysis::LoudnessAnalyser)
    // Assemble AnalysisSnapshot, post to worker_.result_
  }
};
```

**AnalysisRequest** (job struct, framework-free):
```cpp
struct AnalysisRequest {
  float magnitudes[AnalysisSnapshot::NumBands];  // FFT energy per band
  float msCorrelation[AnalysisSnapshot::NumBands];  // M/S phase correlation
  // Derived from audio thread's FFT run
};
```

**AnalysisSnapshot** (result struct, already in common/analysis):
- Per-band resonance peaks (frequency, Q, magnitude)
- Per-band loudness (LUFS)
- Per-band mono correlation (0–1)
- Overall loudness + dynamic range
- Advice set (derived from common::analysis::deriveAdvice)

### 2.3 Data Flow

```
Audio thread (processBlock):
  ├─ Consume input audio
  ├─ Run FFT (common::dsp::Fft) → freq magnitudes
  ├─ M/S decode → correlation per band
  ├─ Assemble AnalysisRequest
  ├─ worker_.submit(request)  ← non-blocking
  ├─ Read worker_.getLatest() → AnalysisSnapshot (atomic, lock-free)
  └─ Update internal state (for DAW sync, etc.)

Resonance worker thread (background):
  Continuously:
    ├─ Read AnalysisRequest from queue
    ├─ common::analysis::ResonancePeakPicker::detect(...)
    ├─ common::analysis::LoudnessAnalyser::compute(...)
    ├─ Assemble AnalysisSnapshot
    └─ Write to atomic<AnalysisSnapshot>

UI thread (Timer ~30 Hz):
  ├─ Read worker_.getLatest() → AnalysisSnapshot
  ├─ Update SpectrumMeter, CorrelationGauge, AdviceLabel widgets
  └─ Repaint
```

### 2.4 Lifecycle

- Worker starts lazily on first `processBlock()` call.
- Worker stops when `ResonanceWorker` destructs (thread joins, queue drains).
- Safe to pause/resume audio — queue backlog drains gracefully, worker sleeps on empty queue.

---

## 3. UI Components: All Extracted to common/hui

### 3.1 New Components

**`common/hui::SpectrumMeter`**
- Display 7 per-band stereo level meters (L/R pairs).
- Per-band reference lines (from current preset's target levels).
- Color-coded: green (in target), yellow (marginal), red (problem).
- Update every ~30 ms from AnalysisSnapshot.

**`common/hui::CorrelationGauge`**
- Single needle gauge showing mono correlation (0–1).
- Color zones: red <0.7 (phase issues), yellow 0.7–0.9 (marginal), green >0.9 (good).
- One gauge per band, or one global gauge — TBD during implementation.
- Update frequency same as SpectrumMeter.

**`common/hui::AdviceLabel`**
- Reusable text feedback component.
- Styled label with settable background color (e.g., red for issues, green for good, gray for neutral).
- Multiple advice items (one per audio concern: muddiness, crispiness, phase cancellation, etc.).
- Update every ~30 ms.

### 3.2 Existing Components (Reused)

**`common/hui::PresetBrowser`** (already in Common as of Phase 1)
- Selector dropdown + Save-As/Delete/Rename buttons.
- Scans both factory presets (embedded) and user presets (from disk).
- Clicking a preset calls DPF's `setParameterValue()` to load it.

### 3.3 Editor Layout

```
┌─────────────────────────────────────────┐
│          Spellbound TrueSight           │
├─────────────────────────────────────────┤
│ Preset: [Indie Rock ▼] [Save] [Delete] │  ← common/hui::PresetBrowser
├─────────────────────────────────────────┤
│  Sub  Low  LM   Mid  HM   High  Air     │  ← Band labels
│  ████ ████ ████ ████ ████ ████ ████    │  ← common/hui::SpectrumMeter (L/R pairs + refs)
│  ████ ████ ████ ████ ████ ████ ████    │
├─────────────────────────────────────────┤
│  Mono Coherence (per band):             │
│  🟢 🟢 🟡 🟢 🟢 🟢 🟢                       │  ← common/hui::CorrelationGauge × 7
├─────────────────────────────────────────┤
│  Advice:                                │
│  • Low-mid muddiness (reduce 200–300 Hz) │  ← common/hui::AdviceLabel × N
│  • Bright peaks in air band (tame 10 kHz) │
└─────────────────────────────────────────┘
```

All custom JUCE `Component`s are replaced with the above Common/hui components. No TrueSight-specific UI code.

---

## 4. Presets: Hybrid Factory + User

### 4.1 Factory Presets (Embedded)

Existing `Presets/*.xml` files (genre profiles) remain. At build time:

```cmake
# CMakeLists.txt
file(GLOB TRUESIGHT_PRESET_FILES "Presets/*.xml")
# Embed paths for common::presets::PresetIO to load at runtime
```

Loaded on plugin init via `common/presets::PresetIO::loadFactoryPresets()`.

### 4.2 User Presets (Disk)

Users can save custom presets to:
- Linux: `~/.config/Spellbound/TrueSight/presets/`
- macOS: `~/Library/Application Support/Spellbound/TrueSight/presets/`
- Windows: `%APPDATA%\Spellbound\TrueSight\presets\`

When editor shows preset browser, `common/presets::PresetIO::loadUserPresets()` scans the user directory and merges with factory presets. Visual distinction: "My Mix (user)" vs. "Indie Rock (factory)".

### 4.3 Preset Format

XML schema (same as Codex, validated by common/presets):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Preset name="Indie Rock" genre="indie">
  <BandTargets>
    <Band index="0" name="Sub" targetLufs="-10" minCorr="0.8"/>
    <!-- ... 7 bands total ... -->
  </BandTargets>
  <EnergyThreshold lufs="-20"/>
  <DynamicRangeMin db="6"/>
</Preset>
```

No TrueSight-specific extensions — the schema is shared (reusable across plugins that do spectral analysis).

### 4.4 State Serialization

On `Plugin::getState()`:
```cpp
std::string presetXml = common::presets::PresetIO::export(currentPreset);
// Serialize presetXml to binary blob, return pointer + size
```

On `Plugin::setState(data, size)`:
```cpp
std::string presetXml = common::presets::PresetIO::deserialize(data, size);
AnalysisSnapshot::Preset preset = common::presets::PresetIO::import(presetXml);
applyPreset(preset);  // Update spectrum targets, correlation thresholds, etc.
```

---

## 5. Testing Strategy

### 5.1 Unit Tests (Framework-Free)

Via CTest, testing DSP components in `common/` (owned by Common, run here too):

- `Tests/test_fft.cpp` — FFT correctness (common/dsp)
- `Tests/test_loudness.cpp` — loudness calculation (common/analysis)
- `Tests/test_resonance_peaks.cpp` — resonance detector (common/analysis)

No JUCE/DPF dependencies. Tests validate the extracted analysis library independently.

### 5.2 Integration Tests (DPF-Aware)

New tests for plugin-specific behavior:

- `Tests/test_plugin_params.cpp` — parameter setters, state round-trip
- `Tests/test_plugin_presets.cpp` — load factory preset, change genre, save user preset
- `Tests/test_plugin_state.cpp` — serialize/deserialize DAW state blob
- `Tests/test_resonance_worker.cpp` — AsyncWorker lifecycle, queue backlog, pause/resume

Link against DPF, run via CTest.

### 5.3 Format Validation (Linux, Gating)

- `pluginval` for VST3
- CLAP validator for CLAP
- `lv2lint` for LV2

All three must pass to close Stage 4B.

### 5.4 Manual Verification (Not Gating)

Load TrueSight in a real DAW (Reaper, Bitwig, etc.) on Linux:
- Audio passes through, meters update in real-time
- Preset browser loads/saves user presets
- Advice labels update correctly on audio change
- Resonance worker doesn't crash on DAW pause/resume/seek

Documented in a manual test checklist, not automated.

---

## 6. CI/CD: Linux Pipeline

### 6.1 GitHub Actions Workflow

Trigger: push to any branch, push to `main`, tag push.

**Steps:**

1. **Setup** — Ubuntu 24.04 (LTS), install deps (libasound2-dev, libjack-jackd2-dev, libx11-dev, libfreetype-dev, libwebkit2gtk-4.1-dev, libglu1-mesa-dev, pluginval, clap-validator, lv2-devel)

2. **Configure** — `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`

3. **Build** — `cmake --build build --parallel` (all targets: VST3, CLAP, LV2, Standalone)

4. **Test** — `ctest --test-dir build --output-on-failure`

5. **Validate** — run pluginval/clap-validator/lv2lint on artifacts

6. **Package (tag push only)** — 
   ```bash
   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
   cmake --build build-release --parallel
   cd build-release && cpack
   ```
   Produces `TrueSight-<version>-linux-x86_64.tar.gz` (tarball only, no `.deb` in Phase 4)

7. **Release** — attach tarball + `.sha256` checksum to GitHub Release (tag)

### 6.2 Not Gating (Phase 5/6)

Per the 2026-08-31 scope change:
- Windows CI (continue-on-error): builds, runs tests, validates. Not gating.
- macOS CI (continue-on-error): builds, runs tests, validates. Not gating.
- `.deb` packaging: deferred to Phase 5/6.
- `auval` (AU validation on macOS): deferred to Phase 5/6.

### 6.3 Exit Criteria for Stage 4B

- Linux CI green (build + test + validators) on all commits to `main`.
- Tarball successfully produced and downloadable from Release.
- No regressions in Common's API (Codex's Phase 4A CI still green).
- Manual verification pass (documented checklist, developer-verified).

---

## 7. Common Modules Affected / Created

### 7.1 common/io

**New:** `AsyncWorker<JobType, ResultType>`
- Owns one background `std::thread`, one `SpscRingBuffer<JobType>`.
- Used by TrueSight's resonance worker, reusable by future plugins.

### 7.2 common/hui

**New:**
- `SpectrumMeter` — 7-band stereo level display with references
- `CorrelationGauge` — mono correlation needle (may be 1 global or 7 per-band, TBD in impl)
- `AdviceLabel` — styled feedback text component

**Existing (reused):**
- `PresetBrowser` — load/save/delete presets UI

### 7.3 common/analysis, common/dsp

No changes. TrueSight consumes the already-extracted modules from Phase 4A.

---

## 8. Implementation Sequence (Addressed in Writing-Plans)

High-level order (detailed plan deferred to superpowers:writing-plans):

1. **Common/io::AsyncWorker** — implement and CTest
2. **Common/hui components** — implement SpectrumMeter, CorrelationGauge, AdviceLabel, CTest
3. **Plugin adapter** — PluginProcessor → DPF Plugin, parameter tree, state handling
4. **Resonance worker** — ResonanceWorker class, integrate AsyncWorker
5. **UI integration** — PluginEditor → DPF UI, embed common/hui components
6. **Presets** — migrate to common/presets, factory + user presets, state serialization
7. **Tests** — CTest suite, integration tests
8. **CI** — GitHub Actions workflow, format validators
9. **Packaging** — CMakeLists.txt CPack for tarball
10. **Manual verification** — load in DAW, test checklist

---

## 9. Risks & Mitigation

| Risk | Mitigation |
|------|-----------|
| DPF's LV2 support immature | Phase 0 spike already validated; include LV2 in CI from day 1 |
| AsyncWorker pattern unproven across plugins | TrueSight is first real consumer; design for future use, test robustly |
| SpectrumMeter/CorrelationGauge UI rendering complex | Build incrementally, test visual output early with manual pass |
| Preset state round-trip fails in DAW | CTest covers serialization; manual DAW verification mandatory |
| Windows/macOS CI missing (deferred to Phase 5) | Builds still run (non-gating); regressions visible but won't block |

---

## 10. Success Criteria

✅ TrueSight builds VST3/CLAP/LV2 on Linux via DPF  
✅ All format validators pass (pluginval, clap-validator, lv2lint)  
✅ CTest suite passes (unit + integration)  
✅ Linux CI green, tarball produced on tag  
✅ Common/io::AsyncWorker extracted and reusable  
✅ All UI components extracted to common/hui  
✅ Preset system converged to common/presets (factory + user)  
✅ Manual DAW verification pass documented  
✅ No regressions in Codex (Phase 4A consumer)  
