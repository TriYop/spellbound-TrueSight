"""Ingestion pipeline: discover audio files, run metadata + DSP analysis, persist to DB."""
from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator

from .analysis import analyze_file
from .db import Database
from .metadata import enrich, is_audio_file

log = logging.getLogger(__name__)


@dataclass
class IngestProgress:
    file_path: str
    status: str       # 'analyzing' | 'done' | 'skipped' | 'error'
    message: str = ""


@dataclass
class IngestSummary:
    added: int = 0
    updated: int = 0
    skipped: int = 0
    errors: int = 0


def discover_files(root: Path, recursive: bool = True) -> list[Path]:
    if root.is_file() and is_audio_file(root):
        return [root]
    pattern = "**/*" if recursive else "*"
    return sorted(p for p in root.glob(pattern) if p.is_file() and is_audio_file(p))


def ingest_files(
    files: list[Path],
    db: Database,
    use_network: bool = True,
    force_reanalysis: bool = False,
    progress_cb: Callable[[IngestProgress], None] | None = None,
) -> IngestSummary:
    summary = IngestSummary()

    for file_path in files:
        progress_cb and progress_cb(IngestProgress(str(file_path), "analyzing"))
        try:
            result = _ingest_one(file_path, db, use_network, force_reanalysis)
            if result == "skipped":
                summary.skipped += 1
                progress_cb and progress_cb(IngestProgress(str(file_path), "skipped"))
            elif result == "updated":
                summary.updated += 1
                progress_cb and progress_cb(IngestProgress(str(file_path), "done", "updated"))
            else:
                summary.added += 1
                progress_cb and progress_cb(IngestProgress(str(file_path), "done", "added"))
        except Exception as exc:
            summary.errors += 1
            log.error("Failed to ingest %s: %s", file_path, exc)
            progress_cb and progress_cb(IngestProgress(str(file_path), "error", str(exc)))

    return summary


def _ingest_one(
    file_path: Path, db: Database, use_network: bool, force_reanalysis: bool
) -> str:
    from .analysis import _hash_file

    current_hash = _hash_file(file_path)
    stored_hash = db.get_track_hash(str(file_path))

    if stored_hash == current_hash and not force_reanalysis:
        existing = db.get_track_by_path(str(file_path))
        if existing and existing["analyzed_at"]:
            return "skipped"

    result = analyze_file(file_path)
    track_id = db.upsert_track(
        str(file_path),
        current_hash,
        result.duration_s,
        result.sample_rate,
        result.channels,
    )

    # Metadata: don't overwrite manual edits
    existing_meta = db.get_metadata(track_id)
    if existing_meta is None or existing_meta["source"] != "manual":
        meta = enrich(file_path, use_network=use_network)
        db.upsert_metadata(
            track_id,
            title=meta.title,
            artist=meta.artist,
            album=meta.album,
            year=meta.year,
            genre=meta.genre,
            mood=meta.mood,
            source=meta.source,
            mb_recording_id=meta.mb_recording_id,
            overwrite_manual=False,
        )

    db.upsert_analysis(
        track_id,
        result.band_rms_db,
        result.band_correlation,
        result.band_transient_db,
        result.overall_rms_db,
        result.overall_correlation,
    )
    db.mark_analyzed(track_id)

    return "updated" if stored_hash else "added"
