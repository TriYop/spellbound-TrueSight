"""Metadata acquisition: ID3/Vorbis tags via mutagen, optionally enriched
via AcoustID fingerprint + MusicBrainz lookup."""
from __future__ import annotations

import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

log = logging.getLogger(__name__)

SUPPORTED_EXTENSIONS = {".mp3", ".flac", ".ogg", ".opus", ".m4a", ".wav", ".aiff", ".aif"}


@dataclass
class TrackMetadata:
    title: str = ""
    artist: str = ""
    album: str = ""
    year: Optional[int] = None
    genre: str = ""
    mood: str = ""
    source: str = "manual"            # 'id3' | 'musicbrainz' | 'manual'
    mb_recording_id: str = ""
    tags: dict = field(default_factory=dict)  # raw extra tags


def read_id3(path: str | Path) -> TrackMetadata:
    """Read tags from audio file using mutagen. Returns empty metadata on failure."""
    try:
        import mutagen
        f = mutagen.File(str(path), easy=True)
        if f is None:
            return TrackMetadata(source="id3")
        get = lambda k: (f.get(k) or [""])[0]
        year_raw = get("date")
        year = None
        try:
            year = int(str(year_raw)[:4]) if year_raw else None
        except ValueError:
            pass
        return TrackMetadata(
            title=get("title"),
            artist=get("artist"),
            album=get("album"),
            year=year,
            genre=get("genre"),
            mood=get("mood") or get("comment") or "",
            source="id3",
        )
    except Exception as exc:
        log.warning("mutagen read failed for %s: %s", path, exc)
        return TrackMetadata(source="id3")


def fingerprint_file(path: str | Path) -> tuple[str, float]:
    """Return (chromaprint_fingerprint, duration_s). Raises on failure."""
    import acoustid
    duration, fp = acoustid.fingerprint_file(str(path))
    return fp, float(duration)


def lookup_musicbrainz(
    fingerprint: str, duration: float, api_key: str = "fgSNtWNvOe"
) -> TrackMetadata:
    """Query AcoustID API and MusicBrainz for metadata. Returns empty on any error.

    Uses the AcoustID community client key as default; supply your own key if needed.
    """
    try:
        import acoustid
        results = acoustid.lookup(api_key, fingerprint, duration,
                                   meta="recordings releasegroups")
        best = None
        best_score = 0.0
        for result in results:
            score = result.get("score", 0.0)
            if score > best_score and result.get("recordings"):
                best_score = score
                best = result

        if best is None or best_score < 0.5:
            return TrackMetadata(source="id3")

        rec = best["recordings"][0]
        mb_id = rec.get("id", "")
        title = rec.get("title", "")
        artists = rec.get("artists", [])
        artist = artists[0].get("name", "") if artists else ""

        rg = rec.get("releasegroups", [{}])[0]
        genres = [g["name"] for g in rg.get("genres", [])]
        genre = genres[0] if genres else ""

        return TrackMetadata(
            title=title,
            artist=artist,
            genre=genre,
            source="musicbrainz",
            mb_recording_id=mb_id,
        )
    except Exception as exc:
        log.warning("MusicBrainz lookup failed: %s", exc)
        return TrackMetadata(source="id3")


def enrich(path: str | Path, use_network: bool = True) -> TrackMetadata:
    """Best-effort metadata: ID3 first, MusicBrainz overlay for missing fields."""
    meta = read_id3(path)

    if not use_network:
        return meta

    try:
        fp, dur = fingerprint_file(path)
        mb = lookup_musicbrainz(fp, dur)
        if mb.title and not meta.title:
            meta.title = mb.title
        if mb.artist and not meta.artist:
            meta.artist = mb.artist
        if mb.genre and not meta.genre:
            meta.genre = mb.genre
        if mb.mb_recording_id:
            meta.mb_recording_id = mb.mb_recording_id
        if mb.source == "musicbrainz":
            meta.source = "musicbrainz"
    except Exception as exc:
        log.debug("Fingerprint enrichment skipped: %s", exc)

    return meta


def is_audio_file(path: Path) -> bool:
    return path.suffix.lower() in SUPPORTED_EXTENSIONS
