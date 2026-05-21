# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MixAdvice is a realtime audio analyzer DAW plugin (pre-mastering indicator) that detects mix issues and emits actionable advice. It is not a mastering tool — it is a diagnostic tool used before mastering.

## Domain: What the Plugin Analyzes

- **Mono compatibility**: Per-band phase cancellation issues (sub, lows, low-mids, mids, hi-mids, highs, air)
- **EQ issues**: Per-band level problems mapped to psychoacoustic perceptions (muddiness, crispiness, etc.)
- **Spectrum fullness**: Coverage gaps across the frequency spectrum
- **Energy levels**: Dynamic range and energy distribution issues

## Presets / Genre Targets

The plugin ships genre-specific reference presets:
- Herbert von Karajan — classical
- Hans Zimmer — hybrid soundtrack
- Fest Noz — traditional (Breton folk)
- Dancefloor — electronic
- Popstar — synth-pop and derivatives
- Singer-songwriter

Each preset defines the expected per-band levels, mono compatibility tolerances, and energy ranges for that genre.

## Build Commands

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

First run downloads JUCE 8.0.13 and clap-juce-extensions 0.26.0 into `build/_deps/` (~200 MB shallow clone, ~2 min).

### Build

```bash
cmake --build build --parallel                        # all targets
cmake --build build --target MixAdvice_Standalone     # standalone only
cmake --build build --target MixAdvice_VST3           # VST3 only
cmake --build build --target MixAdvice_CLAP           # CLAP only
```

### Run standalone

```bash
./build/MixAdvice_artefacts/Debug/Standalone/MixAdvice
```

### Install plugins (Linux, dev build)

```bash
cp -r build/MixAdvice_artefacts/Debug/VST3/MixAdvice.vst3 ~/.vst3/
cp    build/MixAdvice_artefacts/Debug/CLAP/MixAdvice.clap ~/.clap/
```

### Create shippable tarball

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
cd build-release && cpack
```

Produces `build-release/MixAdvice-<version>-linux-x86_64.tar.gz` (and a `.sha256` checksum) containing:

```
MixAdvice-<version>-linux-x86_64/
├── install.sh      ← run to install (user) or --system (root)
├── uninstall.sh    ← removes from all known locations
├── bin/MixAdvice   ← standalone app
├── CLAP/MixAdvice.clap
└── VST3/MixAdvice.vst3/
```

### End-user installation from tarball

```bash
tar -xzf MixAdvice-<version>-linux-x86_64.tar.gz
cd MixAdvice-<version>-linux-x86_64
./install.sh            # installs to ~/.vst3, ~/.clap, ~/.local/bin
./install.sh --system   # installs system-wide (requires sudo)
```

## Architecture

**Source layout:**

```
Source/
  PluginProcessor.h/.cpp   — AudioProcessor; analysis pipeline entry point, preset state
  PluginEditor.h/.cpp      — AudioProcessorEditor; all UI code
  Analysis/                — (future) FFT engine, M/S decoder, per-band correlator
  Presets/                 — (future) genre profile data structs and loader
  UI/                      — (future) custom JUCE Components (meters, advice labels)
```

**Data flow (target architecture):**

```
processBlock()
  └─ Analysis pipeline (real-time, lock-free)
       ├─ FFT → per-band energy levels
       ├─ M/S decode → per-band mono correlation
       └─ Results posted to UI thread via lock-free queue
           └─ PluginEditor timer callback → reads results → repaints meters + advice text
```

**Preset system:** Each genre preset defines per-band target levels, acceptable mono-correlation ranges, and energy thresholds. `getNumPrograms()` / `setCurrentProgram()` are the DAW-facing API; the six slots map to the genre presets listed above.
