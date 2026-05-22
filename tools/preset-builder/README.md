# MixAdvice Preset Builder

Standalone TUI tool for building genre presets from real audio files.

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

### Batch ingest (no UI)
```bash
uv run preset-builder --ingest /path/to/music/folder
uv run preset-builder --ingest track.flac --no-network   # skip AcoustID lookup
uv run preset-builder --ingest /music --force            # re-analyze everything
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
