"""SQLite persistence layer.  All SQL lives here; no SQL outside this module."""
from __future__ import annotations

import json
import sqlite3
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Generator, Optional

# ── Paths ─────────────────────────────────────────────────────────────────────

def default_db_path() -> Path:
    p = Path.home() / ".config" / "MixAdvice" / "preset_builder.db"
    p.parent.mkdir(parents=True, exist_ok=True)
    return p


# ── Schema ────────────────────────────────────────────────────────────────────

_DDL = """
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS tracks (
    id          INTEGER PRIMARY KEY,
    file_path   TEXT    NOT NULL UNIQUE,
    file_hash   TEXT,
    duration_s  REAL,
    sample_rate INTEGER,
    channels    INTEGER,
    added_at    TEXT DEFAULT (datetime('now')),
    analyzed_at TEXT
);

CREATE TABLE IF NOT EXISTS track_metadata (
    track_id        INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    title           TEXT,
    artist          TEXT,
    album           TEXT,
    year            INTEGER,
    genre           TEXT,
    mood            TEXT,
    source          TEXT,
    mb_recording_id TEXT,
    updated_at      TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS track_analysis (
    track_id            INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    band_rms_db         TEXT NOT NULL,
    band_correlation    TEXT NOT NULL,
    band_transient_db   TEXT NOT NULL,
    overall_rms_db      REAL NOT NULL,
    overall_correlation REAL NOT NULL,
    analyzed_at         TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS presets (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    description TEXT,
    xml_path    TEXT,
    track_count INTEGER,
    created_at  TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS preset_tracks (
    preset_id INTEGER REFERENCES presets(id) ON DELETE CASCADE,
    track_id  INTEGER REFERENCES tracks(id)  ON DELETE CASCADE,
    PRIMARY KEY (preset_id, track_id)
);
"""


# ── Database ──────────────────────────────────────────────────────────────────

class Database:
    def __init__(self, path: Path | str | None = None) -> None:
        self._path = Path(path) if path else default_db_path()
        self._conn: sqlite3.Connection | None = None

    # ── Connection management ──────────────────────────────────────────────

    def open(self) -> None:
        # check_same_thread=False is safe here: all writes are wrapped in
        # explicit transactions and the WAL journal handles concurrent access.
        self._conn = sqlite3.connect(str(self._path), check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.executescript(_DDL)

    def close(self) -> None:
        if self._conn:
            self._conn.close()
            self._conn = None

    def __enter__(self) -> "Database":
        self.open()
        return self

    def __exit__(self, *_: Any) -> None:
        self.close()

    @contextmanager
    def transaction(self) -> Generator[sqlite3.Connection, None, None]:
        assert self._conn, "Database not open"
        with self._conn:
            yield self._conn

    # ── Tracks ─────────────────────────────────────────────────────────────

    def upsert_track(
        self, file_path: str, file_hash: str, duration_s: float,
        sample_rate: int, channels: int
    ) -> int:
        with self.transaction() as c:
            cur = c.execute(
                """INSERT INTO tracks (file_path, file_hash, duration_s, sample_rate, channels)
                   VALUES (?, ?, ?, ?, ?)
                   ON CONFLICT(file_path) DO UPDATE SET
                       file_hash=excluded.file_hash,
                       duration_s=excluded.duration_s,
                       sample_rate=excluded.sample_rate,
                       channels=excluded.channels""",
                (file_path, file_hash, duration_s, sample_rate, channels),
            )
            if cur.lastrowid and cur.lastrowid != 0:
                return cur.lastrowid
            row = c.execute("SELECT id FROM tracks WHERE file_path=?", (file_path,)).fetchone()
            return row["id"]

    def get_track_by_path(self, file_path: str) -> Optional[sqlite3.Row]:
        assert self._conn
        return self._conn.execute(
            "SELECT * FROM tracks WHERE file_path=?", (file_path,)
        ).fetchone()

    def get_track_hash(self, file_path: str) -> Optional[str]:
        assert self._conn
        row = self._conn.execute(
            "SELECT file_hash FROM tracks WHERE file_path=?", (file_path,)
        ).fetchone()
        return row["file_hash"] if row else None

    def list_tracks(self) -> list[sqlite3.Row]:
        assert self._conn
        return self._conn.execute("""
            SELECT t.id, t.file_path, t.duration_s, t.analyzed_at,
                   m.title, m.artist, m.album, m.year, m.genre, m.mood, m.source
            FROM tracks t
            LEFT JOIN track_metadata m ON m.track_id = t.id
            ORDER BY m.artist, m.title, t.file_path
        """).fetchall()

    def mark_analyzed(self, track_id: int) -> None:
        with self.transaction() as c:
            c.execute(
                "UPDATE tracks SET analyzed_at=datetime('now') WHERE id=?", (track_id,)
            )

    def delete_track(self, track_id: int) -> None:
        with self.transaction() as c:
            c.execute("DELETE FROM tracks WHERE id=?", (track_id,))

    # ── Metadata ───────────────────────────────────────────────────────────

    def upsert_metadata(
        self, track_id: int, *, title: str = "", artist: str = "",
        album: str = "", year: Optional[int] = None, genre: str = "",
        mood: str = "", source: str = "manual", mb_recording_id: str = "",
        overwrite_manual: bool = False,
    ) -> None:
        assert self._conn
        existing = self._conn.execute(
            "SELECT source FROM track_metadata WHERE track_id=?", (track_id,)
        ).fetchone()
        if existing and existing["source"] == "manual" and not overwrite_manual:
            return
        with self.transaction() as c:
            c.execute("""
                INSERT INTO track_metadata
                    (track_id, title, artist, album, year, genre, mood, source, mb_recording_id)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(track_id) DO UPDATE SET
                    title=excluded.title, artist=excluded.artist,
                    album=excluded.album, year=excluded.year,
                    genre=excluded.genre, mood=excluded.mood,
                    source=excluded.source, mb_recording_id=excluded.mb_recording_id,
                    updated_at=datetime('now')
            """, (track_id, title, artist, album, year, genre, mood, source, mb_recording_id))

    def get_metadata(self, track_id: int) -> Optional[sqlite3.Row]:
        assert self._conn
        return self._conn.execute(
            "SELECT * FROM track_metadata WHERE track_id=?", (track_id,)
        ).fetchone()

    # ── Analysis ───────────────────────────────────────────────────────────

    def upsert_analysis(
        self, track_id: int,
        band_rms_db: list[float],
        band_correlation: list[float],
        band_transient_db: list[float],
        overall_rms_db: float,
        overall_correlation: float,
    ) -> None:
        with self.transaction() as c:
            c.execute("""
                INSERT INTO track_analysis
                    (track_id, band_rms_db, band_correlation, band_transient_db,
                     overall_rms_db, overall_correlation)
                VALUES (?, ?, ?, ?, ?, ?)
                ON CONFLICT(track_id) DO UPDATE SET
                    band_rms_db=excluded.band_rms_db,
                    band_correlation=excluded.band_correlation,
                    band_transient_db=excluded.band_transient_db,
                    overall_rms_db=excluded.overall_rms_db,
                    overall_correlation=excluded.overall_correlation,
                    analyzed_at=datetime('now')
            """, (
                track_id,
                json.dumps(band_rms_db),
                json.dumps(band_correlation),
                json.dumps(band_transient_db),
                overall_rms_db,
                overall_correlation,
            ))

    def get_analysis(self, track_id: int) -> Optional[sqlite3.Row]:
        assert self._conn
        return self._conn.execute(
            "SELECT * FROM track_analysis WHERE track_id=?", (track_id,)
        ).fetchone()

    def get_analyses_for_tracks(self, track_ids: list[int]) -> list[dict]:
        assert self._conn
        rows = []
        for tid in track_ids:
            row = self._conn.execute(
                "SELECT * FROM track_analysis WHERE track_id=?", (tid,)
            ).fetchone()
            if row:
                rows.append({
                    "track_id": tid,
                    "band_rms_db": json.loads(row["band_rms_db"]),
                    "band_correlation": json.loads(row["band_correlation"]),
                    "band_transient_db": json.loads(row["band_transient_db"]),
                    "overall_rms_db": row["overall_rms_db"],
                    "overall_correlation": row["overall_correlation"],
                })
        return rows

    # ── Presets ────────────────────────────────────────────────────────────

    def save_preset(
        self, name: str, description: str, xml_path: str, track_ids: list[int]
    ) -> int:
        with self.transaction() as c:
            cur = c.execute("""
                INSERT INTO presets (name, description, xml_path, track_count)
                VALUES (?, ?, ?, ?)
                ON CONFLICT(name) DO UPDATE SET
                    description=excluded.description,
                    xml_path=excluded.xml_path,
                    track_count=excluded.track_count,
                    created_at=datetime('now')
            """, (name, description, xml_path, len(track_ids)))
            preset_id = cur.lastrowid or c.execute(
                "SELECT id FROM presets WHERE name=?", (name,)
            ).fetchone()["id"]
            c.execute("DELETE FROM preset_tracks WHERE preset_id=?", (preset_id,))
            c.executemany(
                "INSERT OR IGNORE INTO preset_tracks (preset_id, track_id) VALUES (?, ?)",
                [(preset_id, tid) for tid in track_ids],
            )
            return preset_id

    def preset_exists(self, name: str) -> bool:
        assert self._conn
        return self._conn.execute(
            "SELECT 1 FROM presets WHERE name=?", (name,)
        ).fetchone() is not None

    def list_presets(self) -> list[sqlite3.Row]:
        assert self._conn
        return self._conn.execute(
            "SELECT * FROM presets ORDER BY name"
        ).fetchall()
