# MixAdvice Preset Builder

Standalone TUI tool for building genre presets from real audio files.

## Status: Ingest and Create Preset are frozen

The sibling project Codex/MasterTweak (`AudioPlugins/Codex`) has its own Qt6/C++
preset builder that has since surpassed this tool — better track-similarity
distance metric, auto-discovery clustering, and TagLib/AcoustID tag enrichment.
Rather than let this tool and Codex's drift further apart, **Ingest and Create
Preset (export) are disabled here** (both in the TUI and via `--ingest`) until
this tool's analysis is backported to match Codex's improvements (percentile
stats, EBU R128 Loudness Range, resonance detection — see the MixAdvice plugin's
`Source/Analysis/` for the reference implementation once that lands).

**Use Codex's Qt6 preset builder to ingest tracks and build/export new presets
for now.** Both tools write to the same `~/.config/MixAdvice/Presets/` directory
and read/write the identical `MixAdvicePreset` XML schema, so presets built with
Codex's tool work here (and in the MixAdvice plugin) without conversion.

**Browse & tag tracks still works read-only** against the existing local
database, for inspecting previously-ingested data.

## What it does

1. **Ingest** audio files (MP3, FLAC, WAV, OGG, M4A…) — extracts per-band RMS, L/R correlation, and crest factor matching the MixAdvice plugin's 7-band analysis.
2. **Tag** tracks with genre, artist, mood — read from ID3/Vorbis tags automatically, optionally enriched via AcoustID fingerprint + MusicBrainz, or edited manually.
3. **Create presets** — select a group of similar tracks, inspect a z-score coloured anomaly report, then export the preset as an XML file that the MixAdvice plugin discovers automatically.

## Setup

```bash
cd tools/preset-builder
uv sync
uv run preset-builder
```

Requires `chromaprint` system library for AcoustID fingerprinting:
```bash
sudo apt install libchromaprint-tools   # Ubuntu/Debian
```

## Usage

### Interactive TUI
```bash
uv run preset-builder
```

### Batch ingest (no UI) — currently disabled, see "Status" above
```bash
uv run preset-builder --ingest /path/to/music/folder   # prints a deprecation notice and exits
```

### Custom DB location
```bash
uv run preset-builder --db /custom/path/presets.db
```

## Workflow

```
Ingest → Browse & Tag → Create Preset → (report) → Export
```

Exported XML files land in `~/.config/MixAdvice/Presets/` and are picked up automatically next time the MixAdvice plugin loads.

## Band analysis

7 bands matching the plugin exactly:

| Band   | Range       |
|--------|-------------|
| Sub    | < 80 Hz     |
| Lows   | 80–250 Hz   |
| Lo-Mid | 250–500 Hz  |
| Mids   | 500–2 kHz   |
| Hi-Mid | 2–6 kHz     |
| Highs  | 6–16 kHz    |
| Air    | > 16 kHz    |

## Preset export format

```xml
<MixAdvicePreset name="My Genre" description="one-liner">
    <BandRmsDb      sub="-22" lows="-18" lomid="-20" mids="-20" himid="-22" highs="-26" air="-30"/>
    <BandMinCorr    sub="0.90" lows="0.82" .../>
    <BandTransientDb sub="8"  lows="12"  .../>
    <Overall        rmsDb="-16" minCorr="0.62"/>
</MixAdvicePreset>
```

## Database

SQLite at `~/.config/MixAdvice/preset_builder.db`. Contains:
- `tracks` — file paths, hashes, duration
- `track_metadata` — artist, title, genre, mood, MusicBrainz ID
- `track_analysis` — per-band RMS, correlation, crest factor
- `presets` + `preset_tracks` — export history and provenance
