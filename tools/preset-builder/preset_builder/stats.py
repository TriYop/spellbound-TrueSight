"""Statistical preset computation and anomaly detection."""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .analysis import NUM_BANDS

_P_LOW = 10   # percentile used for conservative minCorr thresholds


@dataclass
class PresetValues:
    band_rms_db: list[float]       # mean per band
    band_min_corr: list[float]     # 10th percentile per band
    band_transient_db: list[float] # median per band
    overall_rms_db: float          # mean
    overall_min_corr: float        # 10th percentile


@dataclass
class AnomalyMatrix:
    """Z-scores for each numerical column across selected tracks."""
    track_ids: list[int]
    # Shape: (N_tracks, 7) for band arrays; (N_tracks,) for scalars
    band_rms_z: list[list[float]]
    band_corr_z: list[list[float]]
    band_transient_z: list[list[float]]
    overall_rms_z: list[float]
    overall_corr_z: list[float]


def _zscore(arr: np.ndarray) -> np.ndarray:
    std = arr.std()
    if std < 1e-10:
        return np.zeros_like(arr)
    return (arr - arr.mean()) / std


def compute_preset(analyses: list[dict]) -> PresetValues:
    """Derive preset parameter values from a list of analysis dicts."""
    if not analyses:
        raise ValueError("No analyses provided")

    rms  = np.array([a["band_rms_db"]       for a in analyses])   # (N, 7)
    corr = np.array([a["band_correlation"]   for a in analyses])   # (N, 7)
    tran = np.array([a["band_transient_db"]  for a in analyses])   # (N, 7)
    o_rms  = np.array([a["overall_rms_db"]      for a in analyses])
    o_corr = np.array([a["overall_correlation"] for a in analyses])

    return PresetValues(
        band_rms_db=[round(float(v), 1) for v in rms.mean(axis=0)],
        band_min_corr=[round(float(v), 3) for v in np.percentile(corr, _P_LOW, axis=0)],
        band_transient_db=[round(float(v), 1) for v in np.median(tran, axis=0)],
        overall_rms_db=round(float(o_rms.mean()), 1),
        overall_min_corr=round(float(np.percentile(o_corr, _P_LOW)), 3),
    )


def compute_anomalies(analyses: list[dict]) -> AnomalyMatrix:
    """Compute z-scores for all numerical values across the selected tracks."""
    rms  = np.array([a["band_rms_db"]       for a in analyses], dtype=float)
    corr = np.array([a["band_correlation"]   for a in analyses], dtype=float)
    tran = np.array([a["band_transient_db"]  for a in analyses], dtype=float)
    o_rms  = np.array([a["overall_rms_db"]      for a in analyses], dtype=float)
    o_corr = np.array([a["overall_correlation"] for a in analyses], dtype=float)

    # Z-score per column
    rms_z  = np.column_stack([_zscore(rms[:, i])  for i in range(NUM_BANDS)])
    corr_z = np.column_stack([_zscore(corr[:, i]) for i in range(NUM_BANDS)])
    tran_z = np.column_stack([_zscore(tran[:, i]) for i in range(NUM_BANDS)])

    return AnomalyMatrix(
        track_ids=[a["track_id"] for a in analyses],
        band_rms_z=rms_z.tolist(),
        band_corr_z=corr_z.tolist(),
        band_transient_z=tran_z.tolist(),
        overall_rms_z=_zscore(o_rms).tolist(),
        overall_corr_z=_zscore(o_corr).tolist(),
    )


def anomaly_color(z: float) -> str:
    """Return a Textual/Rich color name for a given z-score magnitude."""
    az = abs(z)
    if az < 1.5:
        return "default"
    if az < 2.5:
        return "yellow"
    return "red"
