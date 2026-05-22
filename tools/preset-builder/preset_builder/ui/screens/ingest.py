"""Ingest screen: path / options form + real-time error log."""
from __future__ import annotations

from pathlib import Path

from textual import on
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
            yield Static("Errors only — successful tracks are not listed here.", id="log-hint")
            yield DataTable(id="log-table")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#log-table", DataTable)
        table.add_columns("File", "Issue")
        self._populate_errors()
        self._sync_button()

    def on_screen_resume(self) -> None:
        self._populate_errors()
        self._sync_button()

    # ── Called by App ──────────────────────────────────────────────────────

    def add_error(self, file_path: str, message: str) -> None:
        """Append one error row — called from App on the main thread."""
        self.query_one("#log-table", DataTable).add_row(
            Path(file_path).name, message
        )

    def on_ingest_finished(self) -> None:
        self._sync_button()

    # ── Internal helpers ───────────────────────────────────────────────────

    def _populate_errors(self) -> None:
        table = self.query_one("#log-table", DataTable)
        table.clear()
        for file_path, message in self.app._error_log:  # type: ignore[attr-defined]
            table.add_row(Path(file_path).name, message)

    def _sync_button(self) -> None:
        running = self.app._ingest_running  # type: ignore[attr-defined]
        btn = self.query_one("#ingest-btn", Button)
        btn.disabled = running
        btn.label = "Ingesting…" if running else "Ingest"

    # ── Events ─────────────────────────────────────────────────────────────

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

        recursive  = self.query_one("#recursive",   Checkbox).value
        use_network = self.query_one("#use-network", Checkbox).value
        force       = self.query_one("#force",       Checkbox).value

        self.query_one("#log-table", DataTable).clear()
        self.app.start_ingest(path, recursive, use_network, force)  # type: ignore[attr-defined]
        self._sync_button()
