"""Main Textual application."""
from __future__ import annotations

from pathlib import Path

from textual import work
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer

from ..db import Database
from .screens.main_menu import MainMenuScreen
from .screens.ingest import IngestScreen
from .screens.browse import BrowseScreen
from .screens.create_preset import CreatePresetScreen


CSS = """
Screen {
    background: #0f0f1a;
}

#title {
    text-style: bold;
    color: #aaaacc;
    margin-bottom: 1;
    text-align: center;
    padding: 1;
}

.field-label {
    color: #6666aa;
    width: auto;
    padding-right: 1;
}

#ingest-layout {
    padding: 1 2;
    height: 100%;
}

#path-row {
    height: 3;
    margin-bottom: 1;
}

#path-row Input {
    width: 1fr;
}

#options-row {
    height: 3;
    margin-bottom: 1;
}

#log-table {
    height: 1fr;
    border: solid #2a2a4e;
}

#log-hint {
    color: #44446a;
    height: 1;
    padding: 0 0 0 0;
    margin-bottom: 0;
}

#filter-row {
    height: 3;
    margin-bottom: 1;
    padding: 0 2;
}

#filter-row Input {
    width: 1fr;
}

#track-table {
    height: 1fr;
    border: solid #2a2a4e;
}

#count-label {
    color: #6666aa;
    height: 1;
    padding: 0 2;
}

#edit-panel {
    padding: 1 2;
    border: solid #44446a;
    margin: 1 2;
}

.edit-row {
    height: 3;
    margin-bottom: 0;
}

.edit-row Input {
    width: 1fr;
}

#edit-buttons {
    height: 3;
    margin-top: 1;
}

#hint {
    color: #aaaacc;
    padding: 1 2;
}

#report-table {
    height: 1fr;
    border: solid #2a2a4e;
}

#export-form {
    padding: 1 2;
    border: solid #44446a;
    margin: 1 2;
    height: auto;
}

#export-form Input {
    margin-bottom: 1;
}

#overwrite-warning {
    height: 1;
    margin-bottom: 1;
}

#status {
    padding: 0 2;
    height: 2;
}

Button {
    margin-right: 1;
}

Middle {
    align: center middle;
}

Center {
    align: center middle;
    width: 40;
}

Center Button {
    width: 100%;
    margin-bottom: 1;
}
"""


class PresetBuilderApp(App):
    TITLE = "MixAdvice Preset Builder"
    CSS = CSS
    SCREENS = {
        "main": MainMenuScreen,
        "ingest": IngestScreen,
        "browse": BrowseScreen,
        "create_preset": CreatePresetScreen,
    }

    def __init__(self, db: Database) -> None:
        super().__init__()
        self.db = db
        # Ingest state — written from main thread only (via call_from_thread)
        self._ingest_running: bool = False
        self._ingest_total: int = 0
        self._ingest_done: int = 0
        self._ingest_error_count: int = 0
        self._error_log: list[tuple[str, str]] = []  # (file_path, message)

    def on_mount(self) -> None:
        self.push_screen("main")

    # ── Public API for screens ─────────────────────────────────────────────

    def start_ingest(self, path: Path, recursive: bool, use_network: bool, force: bool) -> None:
        if self._ingest_running:
            self.notify("Ingest already in progress", severity="warning")
            return
        self._error_log.clear()
        self._ingest_total = 0
        self._ingest_done = 0
        self._ingest_error_count = 0
        self._ingest_running = True
        self._refresh_status()
        self._do_ingest(path, recursive, use_network, force)

    # ── Background worker ──────────────────────────────────────────────────

    @work(thread=True)
    def _do_ingest(self, path: Path, recursive: bool, use_network: bool, force: bool) -> None:
        from ..ingest import discover_files, ingest_files, IngestProgress

        files = discover_files(path, recursive=recursive)
        if not files:
            self.call_from_thread(self._finish_ingest, 0, 0, 0, 0)
            return

        self.call_from_thread(self._set_total, len(files))

        done: list[int] = [0]

        def on_progress(p: IngestProgress) -> None:
            if p.status in ("done", "skipped", "error"):
                done[0] += 1
                self.call_from_thread(
                    self._on_progress, done[0], p.file_path, p.status, p.message
                )

        summary = ingest_files(
            files, self.db, use_network=use_network,
            force_reanalysis=force, progress_cb=on_progress,
        )
        self.call_from_thread(
            self._finish_ingest, summary.added, summary.updated, summary.skipped, summary.errors
        )

    # ── Main-thread callbacks ──────────────────────────────────────────────

    def _set_total(self, total: int) -> None:
        self._ingest_total = total
        self._refresh_status()

    def _on_progress(self, done: int, file_path: str, status: str, message: str) -> None:
        self._ingest_done = done
        if status == "error":
            self._ingest_error_count += 1
            self._error_log.append((file_path, message))
            if isinstance(self.screen, IngestScreen):
                self.screen.add_error(file_path, message)
        self._refresh_status()

    def _finish_ingest(self, added: int, updated: int, skipped: int, errors: int) -> None:
        self._ingest_running = False
        self._refresh_status()
        if isinstance(self.screen, IngestScreen):
            self.screen.on_ingest_finished()
        severity = "warning" if errors else "information"
        self.notify(
            f"Ingest done — {added} added  {updated} updated  "
            f"{skipped} skipped  {errors} errors",
            severity=severity,
        )

    def _refresh_status(self) -> None:
        if self._ingest_running:
            pct = (
                f"{self._ingest_done}/{self._ingest_total}"
                if self._ingest_total else "…"
            )
            err = f" · {self._ingest_error_count} errors" if self._ingest_error_count else ""
            self.sub_title = f"Ingesting {pct}{err}"
        elif self._ingest_error_count:
            self.sub_title = f"Last ingest: {self._ingest_error_count} error(s)"
        else:
            self.sub_title = ""
