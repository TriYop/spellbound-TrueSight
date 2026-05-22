"""DSP analysis: extract per-band RMS, L/R correlation, and crest factor from audio files.

Band layout matches the MixAdvice plugin crossovers exactly:
  Sub      <  80 Hz
  Lows    80–250 Hz
  Lo-Mid 250–500 Hz
  Mids   500–2000 Hz
  Hi-Mid 2000–6000 Hz
  Highs  6000–16000 Hz
  Air     > 16000 Hz
"""
from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt

# Band crossover frequencies in Hz (plugin-matching)
CROSSOVERS = [80.0, 250.0, 500.0, 2000.0, 6000.0, 16000.0]
NUM_BANDS = 7
BAND_NAMES = ["sub", "lows", "lomid", "mids", "himid", "highs", "air"]

_MIN_DB = -90.0  # floor for silent bands
_FILTER_ORDER = 4


@dataclass
class AnalysisResult:
    band_rms_db: list[float]          # per-band RMS level in dBFS (averaged L+R)
    band_correlation: list[float]     # per-band Pearson L/R correlation [-1, 1]
    band_transient_db: list[float]    # per-band crest factor in dB
    overall_rms_db: float
    overall_correlation: float
    duration_s: float
    sample_rate: int
    channels: int
    file_hash: str


def _rms(x: np.ndarray) -> float:
    v = np.sqrt(np.mean(x ** 2))
    return float(v) if v > 0 else 0.0


def _to_db(linear: float) -> float:
    return float(20.0 * np.log10(max(linear, 1e-10)))


def _bandpass(signal: np.ndarray, low: float | None, high: float | None, fs: int) -> np.ndarray:
    nyq = fs / 2.0
    if low is None:
        # lowpass
        sos = butter(_FILTER_ORDER, high / nyq, btype="low", output="sos")
    elif high is None:
        # highpass
        sos = butter(_FILTER_ORDER, low / nyq, btype="high", output="sos")
    else:
        sos = butter(_FILTER_ORDER, [low / nyq, high / nyq], btype="band", output="sos")
    return sosfiltfilt(sos, signal)


def _band_limits(band_idx: int) -> tuple[float | None, float | None]:
    low = None if band_idx == 0 else CROSSOVERS[band_idx - 1]
    high = None if band_idx == NUM_BANDS - 1 else CROSSOVERS[band_idx]
    return low, high


def analyze_file(path: str | Path) -> AnalysisResult:
    path = Path(path)

    file_hash = _hash_file(path)

    audio, sr = sf.read(str(path), dtype="float32", always_2d=True)
    channels = audio.shape[1]

    # Downmix to stereo if needed; keep mono as dual-mono
    if channels >= 2:
        L = audio[:, 0].astype(np.float64)
        R = audio[:, 1].astype(np.float64)
    else:
        L = audio[:, 0].astype(np.float64)
        R = L.copy()

    duration_s = len(L) / sr

    band_rms_db: list[float] = []
    band_correlation: list[float] = []
    band_transient_db: list[float] = []

    for i in range(NUM_BANDS):
        low, high = _band_limits(i)

        # Skip band if its range exceeds Nyquist
        nyq = sr / 2.0
        if low is not None and low >= nyq:
            band_rms_db.append(_MIN_DB)
            band_correlation.append(1.0)
            band_transient_db.append(0.0)
            continue
        if high is not None:
            high = min(high, nyq * 0.99)

        bL = _bandpass(L, low, high, sr)
        bR = _bandpass(R, low, high, sr)

        rms_l = _rms(bL)
        rms_r = _rms(bR)
        avg_rms = (rms_l + rms_r) / 2.0
        band_rms_db.append(_to_db(avg_rms))

        # Pearson correlation between L and R within this band
        if rms_l > 1e-10 and rms_r > 1e-10:
            corr = float(np.corrcoef(bL, bR)[0, 1])
            corr = float(np.clip(corr, -1.0, 1.0))
        else:
            corr = 1.0  # silent bands are trivially correlated
        band_correlation.append(corr)

        # Crest factor on the louder channel — only meaningful when band has real energy.
        # Threshold: band RMS must be at least -80 dBFS (1e-4 linear) to avoid
        # numerical-noise producing absurd crest factors in silent bands.
        louder = bL if rms_l >= rms_r else bR
        rms_v = _rms(louder)
        peak = float(np.max(np.abs(louder))) if len(louder) > 0 else 0.0
        if rms_v > 1e-3:  # require band ≥ -60 dBFS to avoid noise-floor artefacts
            crest_db = float(20.0 * np.log10(max(peak / rms_v, 1.0)))
        else:
            crest_db = 0.0
        band_transient_db.append(round(crest_db, 2))

    # Overall metrics
    mix = (L + R) / 2.0
    overall_rms_db = _to_db(_rms(mix))
    if _rms(L) > 1e-10 and _rms(R) > 1e-10:
        overall_corr = float(np.clip(np.corrcoef(L, R)[0, 1], -1.0, 1.0))
    else:
        overall_corr = 1.0

    return AnalysisResult(
        band_rms_db=[round(v, 2) for v in band_rms_db],
        band_correlation=[round(v, 4) for v in band_correlation],
        band_transient_db=[round(v, 2) for v in band_transient_db],
        overall_rms_db=round(overall_rms_db, 2),
        overall_correlation=round(overall_corr, 4),
        duration_s=round(duration_s, 2),
        sample_rate=sr,
        channels=channels,
        file_hash=file_hash,
    )


def _hash_file(path: Path, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while data := f.read(chunk):
            h.update(data)
    return h.hexdigest()
