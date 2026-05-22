"""Browse & tag screen: list all tracks, edit metadata inline."""
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


class BrowseScreen(Screen):
    BINDINGS = [
        ("escape", "app.pop_screen", "Back"),
        ("r", "refresh", "Refresh"),
        ("d", "delete_track", "Delete"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._rows: list = []  # sqlite3.Row list

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical():
            with Horizontal(id="filter-row"):
                yield Label("Filter:", classes="field-label")
                yield Input(placeholder="artist / genre / mood ...", id="filter-input")
            yield DataTable(id="track-table", cursor_type="row")
            yield Static("", id="count-label")
            with Vertical(id="edit-panel"):
                yield Label("Edit metadata for selected track", id="edit-heading")
                with Horizontal(classes="edit-row"):
                    yield Label("Title:", classes="field-label")
                    yield Input(id="edit-title")
                with Horizontal(classes="edit-row"):
                    yield Label("Artist:", classes="field-label")
                    yield Input(id="edit-artist")
                with Horizontal(classes="edit-row"):
                    yield Label("Genre:", classes="field-label")
                    yield Input(id="edit-genre")
                with Horizontal(classes="edit-row"):
                    yield Label("Mood:", classes="field-label")
                    yield Input(id="edit-mood")
                with Horizontal(id="edit-buttons"):
                    yield Button("Save", id="save-meta", variant="primary")
                    yield Button("Cancel", id="cancel-meta", variant="default")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#track-table", DataTable)
        table.add_columns(
            "Artist", "Title", "Genre", "Mood", "Duration", "Analyzed", "Source"
        )
        self._load_tracks()
        self._toggle_edit(False)

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
            dur = f"{r['duration_s']:.0f}s" if r["duration_s"] else "?"
            analyzed = "✓" if r["analyzed_at"] else "–"
            table.add_row(
                r["artist"] or "–",
                r["title"] or r["file_path"].split("/")[-1],
                r["genre"] or "–",
                r["mood"] or "–",
                dur,
                analyzed,
                r["source"] or "–",
                key=str(r["id"]),
            )
        self.query_one("#count-label", Static).update(f"{len(self._rows)} tracks")

    @on(Input.Changed, "#filter-input")
    def on_filter(self, event: Input.Changed) -> None:
        self._load_tracks(event.value)

    @on(DataTable.RowSelected)
    def on_row_selected(self, event: DataTable.RowSelected) -> None:
        track_id = int(event.row_key.value)  # type: ignore[arg-type]
        db = self.app.db  # type: ignore[attr-defined]
        meta = db.get_metadata(track_id)
        self.query_one("#edit-title",  Input).value = (meta["title"]  or "") if meta else ""
        self.query_one("#edit-artist", Input).value = (meta["artist"] or "") if meta else ""
        self.query_one("#edit-genre",  Input).value = (meta["genre"]  or "") if meta else ""
        self.query_one("#edit-mood",   Input).value = (meta["mood"]   or "") if meta else ""
        self.query_one("#edit-heading", Label).update(
            f"Editing: {event.row_key.value}"
        )
        self._toggle_edit(True)
        self._selected_track_id = track_id

    @on(Button.Pressed, "#save-meta")
    def on_save(self) -> None:
        if not hasattr(self, "_selected_track_id"):
            return
        db = self.app.db  # type: ignore[attr-defined]
        db.upsert_metadata(
            self._selected_track_id,
            title=self.query_one("#edit-title",  Input).value,
            artist=self.query_one("#edit-artist", Input).value,
            genre=self.query_one("#edit-genre",  Input).value,
            mood=self.query_one("#edit-mood",   Input).value,
            source="manual",
            overwrite_manual=True,
        )
        self._toggle_edit(False)
        self._load_tracks(self.query_one("#filter-input", Input).value)
        self.notify("Metadata saved", severity="information")

    @on(Button.Pressed, "#cancel-meta")
    def on_cancel(self) -> None:
        self._toggle_edit(False)

    def action_refresh(self) -> None:
        self._load_tracks(self.query_one("#filter-input", Input).value)

    def action_delete_track(self) -> None:
        table = self.query_one("#track-table", DataTable)
        row_key = table.cursor_row
        if row_key is None:
            return
        cell = table.get_row_at(row_key)
        # The row key encodes the track id
        cursor_rk = table.cursor_coordinate
        row_keys = [rk for rk, _ in table.rows.items()]
        if cursor_rk.row < len(row_keys):
            track_id = int(row_keys[cursor_rk.row].value)  # type: ignore[arg-type]
            self.app.db.delete_track(track_id)  # type: ignore[attr-defined]
            self._load_tracks(self.query_one("#filter-input", Input).value)
            self.notify("Track removed from database")

    def _toggle_edit(self, visible: bool) -> None:
        panel = self.query_one("#edit-panel", Vertical)
        panel.display = visible
