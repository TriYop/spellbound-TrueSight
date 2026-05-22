"""Ingest screen: pick directory/files, show per-file progress, report summary."""
from __future__ import annotations

import threading
from pathlib import Path

from textual import on, work
from textual.app import ComposeResult
from textual.containers import Horizontal, Vertical
from textual.screen import Screen
from textual.widgets import (
    Button,
    Checkbox,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    ProgressBar,
    Static,
)


class IngestScreen(Screen):
    BINDINGS = [("escape", "app.pop_screen", "Back")]

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical(id="ingest-layout"):
            yield Label("Path (file or directory):", classes="field-label")
            with Horizontal(id="path-row"):
                yield Input(placeholder="/path/to/audio/files", id="path-input")
                yield Button("Ingest", id="ingest-btn", variant="primary")
            with Horizontal(id="options-row"):
                yield Checkbox("Recursive", id="recursive", value=True)
                yield Checkbox("Network metadata (AcoustID)", id="use-network", value=True)
                yield Checkbox("Force re-analysis", id="force", value=False)
            yield ProgressBar(id="progress", total=100, show_eta=False)
            yield Static("", id="progress-label")
            yield DataTable(id="log-table")
            yield Static("", id="summary-label")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#log-table", DataTable)
        table.add_columns("File", "Status", "Note")
        self.query_one("#progress", ProgressBar).visible = False

    @on(Button.Pressed, "#ingest-btn")
    def start_ingest(self) -> None:
        path_str = self.query_one("#path-input", Input).value.strip()
        if not path_str:
            self.notify("Enter a path first", severity="warning")
            return
        path = Path(path_str)
        if not path.exists():
            self.notify(f"Path not found: {path}", severity="error")
            return

        recursive = self.query_one("#recursive", Checkbox).value
        use_network = self.query_one("#use-network", Checkbox).value
        force = self.query_one("#force", Checkbox).value

        self._run_ingest(path, recursive, use_network, force)

    @work(thread=True)
    def _run_ingest(
        self, path: Path, recursive: bool, use_network: bool, force: bool
    ) -> None:
        from ...ingest import discover_files, ingest_files
        from ...ingest import IngestProgress

        db = self.app.db  # type: ignore[attr-defined]

        files = discover_files(path, recursive=recursive)
        if not files:
            self.call_from_thread(self.notify, "No audio files found", severity="warning")
            return

        total = len(files)
        self.app.call_from_thread(self._setup_progress, total)

        done = [0]

        def on_progress(p: IngestProgress) -> None:
            if p.status in ("done", "skipped", "error"):
                done[0] += 1
                self.app.call_from_thread(
                    self._update_progress, done[0], total, p.file_path, p.status, p.message
                )

        summary = ingest_files(
            files, db, use_network=use_network, force_reanalysis=force,
            progress_cb=on_progress
        )

        self.app.call_from_thread(
            self._show_summary,
            summary.added, summary.updated, summary.skipped, summary.errors
        )

    def _setup_progress(self, total: int) -> None:
        bar = self.query_one("#progress", ProgressBar)
        bar.visible = True
        bar.total = total
        bar.progress = 0
        table = self.query_one("#log-table", DataTable)
        table.clear()

    def _update_progress(
        self, done: int, total: int, path: str, status: str, note: str
    ) -> None:
        bar = self.query_one("#progress", ProgressBar)
        bar.progress = done
        label = self.query_one("#progress-label", Static)
        label.update(f"{done}/{total}")
        filename = Path(path).name
        status_str = {"done": "✓", "skipped": "–", "error": "✗"}.get(status, status)
        self.query_one("#log-table", DataTable).add_row(filename, status_str, note)

    def _show_summary(self, added: int, updated: int, skipped: int, errors: int) -> None:
        self.query_one("#summary-label", Static).update(
            f"Done — added: {added}  updated: {updated}  skipped: {skipped}  errors: {errors}"
        )
        self.query_one("#progress", ProgressBar).visible = False
