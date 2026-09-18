# Stage 4B Manual Verification Checklist

Load `build/bin/TrueSight.vst3` (or `.clap`) in a real Linux host (Reaper, Bitwig, or DPF's own `jalv` for LV2) and confirm:

- [ ] Audio passes through unmodified (A/B against a bypassed track -- TrueSight is a pure analyzer, never alters the signal).
- [ ] Spectrum meter bars update in real time as audio plays; reference lines reflect the currently selected preset's `bandRmsDb` targets.
- [ ] Correlation gauges move toward green for in-phase (mono-compatible) material and red for out-of-phase test material (e.g. an inverted-R test file).
- [ ] Preset selector lists every factory preset (compare count/names against `Presets/*.xml`) plus any preset dropped into `~/.config/MixAdvice/Presets/`; selecting one updates the reference lines and mastering-advice panel.
- [ ] Mastering-advice panel shows non-zero EQ/comp numbers once audio has played past the warm-up window (2s -- `kPercentileWarmupSec` in `Source/TrueSightUI.cpp`, carried over from `Source/_juce_reference/PluginEditor.cpp`'s original constant in Task 8, not this plan's original 5s placeholder), and the "Play audio..." warning disappears once it does.
- [ ] Resonance-cut list updates every few seconds while a resonant test tone plays, and clears/resets when playback restarts (host stop/rewind/play).
- [ ] Plugin survives host pause/resume/seek without crashing or hanging (exercises `ResonanceWorker`'s queue-backlog-drains-gracefully design from Task 3).
- [ ] Preset selection persists across a project save/reload (confirms the automatable `preset` parameter's default host-side state save covers what the old JUCE `getStateInformation`/`setStateInformation` used to do explicitly -- see this plan's Deviation 1).
- [ ] Note whether the dropped DAW-transport-exact play/stop gating (Task 6 Step 2's documented behavior change, signal-RMS gate instead of `getPlayHead()`) is noticeable in practice (e.g. very quiet passages briefly toggling the gate) -- file a follow-up issue if so, don't block Stage 4B on it.

Record the outcome (pass/fail per line, plus host name/version used) in this file and commit it once verification is complete -- this is the "manual verification pass" that Stage 4B's exit criteria (roadmap `Verification approach` section) require documented, not just performed.
