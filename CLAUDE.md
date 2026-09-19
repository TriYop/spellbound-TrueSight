# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TrueSight is a realtime audio analyzer DAW plugin (pre-mastering indicator) that detects mix issues and emits actionable advice. It is not a mastering tool — it is a diagnostic tool used before mastering.

## Domain: What the Plugin Analyzes

- **Mono compatibility**: Per-band phase cancellation issues (sub, lows, low-mids, mids, hi-mids, highs, air)
- **EQ issues**: Per-band level problems mapped to psychoacoustic perceptions (muddiness, crispiness, etc.)
- **Spectrum fullness**: Coverage gaps across the frequency spectrum
- **Energy levels**: Dynamic range and energy distribution issues

## Presets / Genre Targets

The plugin ships a curated library of reference presets built from real analyzed tracks (`Presets/*.xml`, embedded at build time — see `CMakeLists.txt`'s `TRUESIGHT_PRESET_FILES` glob). Each preset defines the expected per-band levels, mono compatibility tolerances, and energy ranges for that reference material. The library is generated with `tools/preset-builder` (see that tool's README) and is expected to grow over time rather than stay fixed to a small archetype list — check `Presets/` for the current set.

## Build Commands

TrueSight was migrated off JUCE onto [DPF](https://github.com/DISTRHO/DPF) +
the first-party `AudioPluginsCommon` library (workspace-wide JUCE→DPF
migration, see `../../CLAUDE.md`). There is no JUCE anywhere in this repo any
more — `Source/_juce_reference/` keeps the old JUCE `PluginProcessor`/
`PluginEditor` sources around purely as a porting reference; they are not
part of any CMake target.

### Linux prerequisites (one-time)

```bash
sudo apt install cmake ninja-build build-essential git \
    libasound2-dev libjack-jackd2-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev \
    libfreetype-dev libfontconfig1-dev \
    libglu1-mesa-dev libwebkit2gtk-4.1-dev
```

### Configure

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

First run `FetchContent`s two git dependencies into `build/_deps/` (no JUCE,
no clap-juce-extensions):

- **DPF**, pinned to a commit SHA on `main` (DPF has no tagged releases) —
  see `AUDIOPLUGINS_DPF_GIT_TAG` in `CMakeLists.txt` for the exact commit,
  shared with the sibling Hex/Pugilist repos that migrated first. Two small
  patches are applied at configure time (`cmake/patches/dpf-clap-*.patch`,
  both real CLAP-validator-caught bugs, not yet reported upstream).
- **`AudioPluginsCommon`** (`github.com/TriYop/spellbound-common`, private
  repo), pinned to tag `v0.8.0` — provides the shared DSP (`SevenBandSplitter`
  etc.), analysis (`AdviceSet`/`deriveAdvice`/`ResonancePeakPicker`), and HUI
  (`hui::dgl::SpectrumMeter`/`CorrelationGauge`/`PresetSelector`/`AdviceLabel`)
  libraries. Two patches fix real compile errors in Common's own v0.8.0
  release (`cmake/patches/common-*.patch`); see each patch file for the
  writeup. Local machines clone it over plain `https` with cached
  git/gh credentials; CI needs `COMMON_REPO_TOKEN` (currently unprovisioned —
  CI has never gone green on this repo for that reason, not a build problem).

Both `FetchContent_Declare(...)` calls only re-run their `PATCH_COMMAND` on a
*fresh* population of `build/_deps/` — delete `build/_deps/dpf-src` or
`build/_deps/audiopluginscommon-src` (or the whole `build/` dir) if you need
to be sure a patch change actually took effect.

### Build

```bash
cmake --build build --parallel      # all targets: VST3 + CLAP + LV2 (dsp+ui)
```

DPF's `dpf_add_plugin(TrueSight ...)` in `CMakeLists.txt` generates one
target per format plus the shared static libs (`TrueSight`, `TrueSight-dsp`,
`TrueSight-ui`) — there is no per-format target name to build in isolation
the way the old JUCE build had `TrueSight_VST3`/`TrueSight_CLAP`, and
**there is no standalone target**: DPF only produces plugin formats here
(`TRUESIGHT_DPF_TARGETS` in `CMakeLists.txt` is `vst3 clap lv2`, plus `au` on
macOS).

Build output lands under `build/bin/`, not `build/TrueSight_artefacts/...`:

```
build/bin/
  TrueSight.vst3/Contents/x86_64-linux/TrueSight.so
  TrueSight.clap
  TrueSight.lv2/TrueSight_dsp.so
  TrueSight.lv2/TrueSight_ui.so
```

### Run

There is no standalone build to run directly — load the VST3, CLAP, or LV2
plugin in a host (e.g. `jalv`/Ardour for LV2, a CLAP host for `.clap`) after
installing it (below).

### Install plugins (Linux, dev build)

```bash
mkdir -p ~/.vst3 ~/.clap ~/.lv2
cp -r build/bin/TrueSight.vst3 ~/.vst3/
cp    build/bin/TrueSight.clap ~/.clap/
cp -r build/bin/TrueSight.lv2  ~/.lv2/
```

(This is what `scripts/install.sh` automates for a release tarball — see
below — it just points at `VST3/`/`CLAP/`/`LV2/` subdirectories instead of
`build/bin/` directly.)

### Create shippable tarball

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
cd build-release && cpack
```

Produces `build-release/TrueSight-<version>-linux-x86_64.tar.gz` (and a
`.sha256` checksum, `<version>` from `project(TrueSight VERSION ...)` in
`CMakeLists.txt`) containing:

```
TrueSight-<version>-linux-x86_64/
├── install.sh      ← run to install (user) or --system (root)
├── uninstall.sh    ← removes from all known locations
├── VST3/TrueSight.vst3/
├── CLAP/TrueSight.clap
├── LV2/TrueSight.lv2/
└── Presets/*.xml   ← reference copies; factory presets are compiled in
                       (see Presets section above), not read from here
```

There is no `bin/TrueSight` standalone binary in the tarball — TrueSight is
plugin-only.

### Create a Debian package

```bash
sudo apt install dpkg-dev
cmake -B build-deb -G Ninja -DCMAKE_BUILD_TYPE=Release -DPACKAGE_DEB=ON
cmake --build build-deb --parallel
cd build-deb && cpack
```

Produces `spellbound-truesight_<version>_<arch>.deb`, installing to
`/usr/lib/{vst3,clap,lv2}` via `dpkg`. Reference preset XMLs land under
`/usr/share/truesight/presets/` instead of the tarball's top-level
`Presets/` (same files, FHS-appropriate location — factory presets are
still compiled in, not read from either location). Requires `dpkg-dev` on
the build host (provides `dpkg-shlibdeps`, which auto-derives the
package's runtime `Depends:`). Uninstall with
`sudo apt remove spellbound-truesight`.

### End-user installation from tarball

```bash
tar -xzf TrueSight-<version>-linux-x86_64.tar.gz
cd TrueSight-<version>-linux-x86_64
./install.sh            # installs VST3/CLAP/LV2 to ~/.vst3, ~/.clap, ~/.lv2
./install.sh --system   # installs system-wide to /usr/lib (requires sudo)
```

`scripts/uninstall.sh` removes from all of the above locations (and their
`/usr/lib` system equivalents when run as root).

## Architecture

**Source layout** (DPF `Plugin`/`UI` split, no JUCE):

```
Source/
  DistrhoPluginInfo.h            — DPF plugin metadata (CLAP ID, unique ID, feature flags)
  TrueSightPluginAdapter.h/.cpp  — DPF Plugin subclass: parameter/state glue,
                                    activate()/deactivate()/run(), wraps
                                    AnalyserEngine + PresetManager
  TrueSightUI.h/.cpp             — DPF UI subclass: builds/lays out the
                                    AudioPluginsCommon::hui_dgl widgets
                                    (SpectrumMeter, CorrelationGauge,
                                    PresetSelector, AdviceLabel) and drives
                                    them from uiIdle()
  Analysis/                      — framework-free DSP, no DPF/JUCE dependency:
                                    AnalyserEngine (per-band RMS/correlation/
                                    crest via SevenBandSplitter),
                                    ResonanceWorker (background-thread FFT
                                    peak-pick), ResonancePeakMath,
                                    AdviceAdapter (bridges AnalysisResult to
                                    Common's AdviceSet/deriveAdvice),
                                    LoudnessAnalyser (EBU R128 LRA), AnalysisResult
                                    (lock-free atomics read by the UI thread)
  Presets/PresetManager.h/.cpp   — framework-free preset store: built-in
                                    presets embedded at configure time (see
                                    below) plus user presets from
                                    ~/.config/MixAdvice/Presets (path
                                    intentionally left unchanged by the
                                    TrueSight rebrand — it's a locked
                                    cross-repo contract with Codex/
                                    MasterTweak's own preset schema, see
                                    ../CLAUDE.md's Cross-Plugin Naming Note)
  UI/MasteringAdvicePanel.h/.cpp — custom NanoVG panel (DGL NanoSubWidget):
                                    per-band EQ/mixbus/loudness numeric
                                    readout + resonance-cut list
  _juce_reference/                — old JUCE PluginProcessor/PluginEditor,
                                    kept only as a porting reference; not
                                    compiled into any target
```

**Built-in presets:** `CMakeLists.txt` globs `Presets/*.xml` at *configure*
time and generates `${CMAKE_BINARY_DIR}/generated/EmbeddedPresets.h/.cpp`
(name → raw XML text), replacing the old `juce_add_binary_data` step.
Editing/adding a preset file requires a re-configure, not just a rebuild.

**DSP/UI link boundary (`FILES_COMMON`/`FILES_DSP`/`FILES_UI` in
`CMakeLists.txt`):** DPF's `<name>-ui` static lib links against `<name>`
(`FILES_COMMON`) but *not* `<name>-dsp`, so anything the UI reaches
non-virtually and non-inline must live in `FILES_COMMON` even if it's
conceptually DSP — this bit both `AdviceAdapter.cpp` and, initially,
`PresetManager.cpp`/`EmbeddedPresets.cpp` (both now correctly in
`FILES_COMMON`).

**Data flow:**

```
run()
  └─ TrueSightPluginAdapter::run() (audio thread, real-time-safe)
       └─ AnalyserEngine::process() — SevenBandSplitter → per-band RMS/peak/
          correlation/crest → AnalysisResult atomics; mono downmix pushed to
          ResonanceWorker's lock-free FIFO
            └─ ResonanceWorker background thread — FFT + peak-pick, publishes
               resonance peaks back into AnalysisResult
                 └─ TrueSightUI::uiIdle() (UI thread, ~30 Hz)
                      ├─ AnalysisResult::read() snapshot
                      ├─ SpectrumMeter / CorrelationGauge / PresetSelector — live meters
                      └─ buildAnalysisSnapshot() → deriveAdvice() +
                         buildResonancePeaks() → MasteringAdvicePanel::update()
```

**Preset system:** `PresetManager` holds the merged built-in + user preset
list; `TrueSightPluginAdapter`'s single automatable parameter
(`kParameterPresetIndex`) is the DAW-facing selector, mirrored into
`TrueSightUI`'s `PresetSelector` widget via `parameterChanged()`/
`onIndexSelected`. Each preset defines per-band target levels and other
thresholds `deriveAdvice()` compares the live analysis against.
