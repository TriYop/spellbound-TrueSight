"""Entry point."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .db import Database


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="preset-builder",
        description="MixAdvice Preset Builder — build genre presets from real audio files",
    )
    parser.add_argument(
        "--db", metavar="PATH",
        help="SQLite database path (default: ~/.config/MixAdvice/preset_builder.db)"
    )
    parser.add_argument(
        "--ingest", metavar="PATH", nargs="+",
        help="Ingest audio files or directories without launching the TUI (batch mode)"
    )
    parser.add_argument(
        "--no-network", action="store_true",
        help="Disable AcoustID / MusicBrainz network lookups"
    )
    parser.add_argument(
        "--force", action="store_true",
        help="Re-analyze files even if already in the database"
    )

    args = parser.parse_args()

    if args.ingest:
        print(
            "Ingest is currently disabled in this tool: its analysis hasn't been\n"
            "updated yet to match Codex/MasterTweak's preset builder. Use Codex's\n"
            "Qt6 preset builder to ingest tracks and build/export presets for now\n"
            "(see tools/preset-builder/README.md).",
            file=sys.stderr,
        )
        sys.exit(1)

    db = Database(args.db)
    db.open()

    # Launch TUI
    from .ui.app import PresetBuilderApp
    app = PresetBuilderApp(db)
    app.run()
    db.close()


def _batch_ingest(paths: list[str], db: Database, use_network: bool, force: bool) -> None:
    from .ingest import discover_files, ingest_files, IngestProgress

    all_files: list[Path] = []
    for p in paths:
        all_files.extend(discover_files(Path(p)))

    print(f"Found {len(all_files)} audio file(s)")

    def progress(p: IngestProgress) -> None:
        icon = {"done": "✓", "skipped": "–", "error": "✗"}.get(p.status, "…")
        print(f"  {icon} {Path(p.file_path).name}  {p.message}")

    summary = ingest_files(
        all_files, db, use_network=use_network, force_reanalysis=force, progress_cb=progress
    )
    print(
        f"\nDone — added: {summary.added}  "
        f"updated: {summary.updated}  "
        f"skipped: {summary.skipped}  "
        f"errors: {summary.errors}"
    )
