"""Create preset screen: filter + multi-select tracks, then proceed to report."""
from __future__ import annotations

from textual import on
from textual.app import ComposeResult
from textual.containers import Horizontal, Vertical
from textual.screen import Screen
from textual.widgets import (
    Button,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    Static,
)


class CreatePresetScreen(Screen):
    BINDINGS = [
        ("escape", "app.pop_screen", "Back"),
        ("space", "toggle_selection", "Toggle select"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._rows: list = []
        self._selected: set[int] = set()
        self._check_col_key: object = None

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical():
            yield Label(
                "Select tracks to include in the new preset (Space to toggle, need ≥ 2)",
                id="hint"
            )
            with Horizontal(id="filter-row"):
                yield Label("Filter:", classes="field-label")
                yield Input(placeholder="artist / genre / mood ...", id="filter-input")
                yield Button("Clear selection", id="clear-btn", variant="default")
            yield DataTable(id="track-table", cursor_type="row")
            yield Static("", id="count-label")
            yield Button(
                "Generate report →", id="report-btn", variant="success", disabled=True
            )
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#track-table", DataTable)
        (self._check_col_key, *_) = table.add_columns("", "Artist", "Title", "Genre", "Mood", "Analyzed")
        self._load_tracks()

    def on_screen_resume(self) -> None:
        self._selected.clear()
        self._load_tracks(self.query_one("#filter-input", Input).value)

    def _load_tracks(self, filter_text: str = "") -> None:
        db = self.app.db  # type: ignore[attr-defined]
        all_rows = db.list_tracks()
        ft = filter_text.lower()
        if ft:
            self._rows = [
                r for r in all_rows
                if ft in (r["artist"] or "").lower()
                or ft in (r["genre"] or "").lower()
                or ft in (r["mood"] or "").lower()
                or ft in (r["title"] or "").lower()
            ]
        else:
            self._rows = list(all_rows)

        table = self.query_one("#track-table", DataTable)
        table.clear()
        for r in self._rows:
            tid = r["id"]
            check = "✓" if tid in self._selected else " "
            analyzed = "✓" if r["analyzed_at"] else "–"
            table.add_row(
                check,
                r["artist"] or "–",
                r["title"] or r["file_path"].split("/")[-1],
                r["genre"] or "–",
                r["mood"] or "–",
                analyzed,
                key=str(tid),
            )
        self._update_count()

    @on(Input.Changed, "#filter-input")
    def on_filter(self, event: Input.Changed) -> None:
        self._load_tracks(event.value)

    def action_toggle_selection(self) -> None:
        table = self.query_one("#track-table", DataTable)
        cursor = table.cursor_coordinate
        row_keys = [rk for rk, _ in table.rows.items()]
        if cursor.row >= len(row_keys):
            return
        track_id = int(row_keys[cursor.row].value)  # type: ignore[arg-type]
        if track_id in self._selected:
            self._selected.discard(track_id)
            table.update_cell(str(track_id), self._check_col_key, " ")
        else:
            db = self.app.db  # type: ignore[attr-defined]
            analysis = db.get_analysis(track_id)
            if not analysis:
                self.notify("Track not yet analyzed — ingest it first", severity="warning")
                return
            self._selected.add(track_id)
            table.update_cell(str(track_id), self._check_col_key, "✓")
        self._update_count()

    @on(Button.Pressed, "#clear-btn")
    def on_clear(self) -> None:
        self._selected.clear()
        self._load_tracks(self.query_one("#filter-input", Input).value)

    @on(Button.Pressed, "#report-btn")
    def on_report(self) -> None:
        from .report import ReportScreen
        self.app.push_screen(ReportScreen(list(self._selected)))

    def _update_count(self) -> None:
        n = len(self._selected)
        self.query_one("#count-label", Static).update(
            f"{len(self._rows)} tracks shown — {n} selected"
        )
        self.query_one("#report-btn", Button).disabled = n < 2
