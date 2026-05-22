"""Report screen: z-score colored table + preset export form."""
from __future__ import annotations

from pathlib import Path

from textual import on
from textual.app import ComposeResult
from textual.containers import Horizontal, Vertical
from textual.css.query import NoMatches
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
from textual.reactive import reactive

from ...analysis import BAND_NAMES, NUM_BANDS
from ...stats import AnomalyMatrix, PresetValues, anomaly_color, compute_anomalies, compute_preset
from ...export import export_preset, preset_xml_path

# Column groups shown in the report table
_BAND_COLS = [f"RMS {b}" for b in BAND_NAMES] + \
             [f"Cor {b}" for b in BAND_NAMES] + \
             [f"Trn {b}" for b in BAND_NAMES]
_SCALAR_COLS = ["OvRMS", "OvCorr"]


class ReportScreen(Screen):
    BINDINGS = [("escape", "app.pop_screen", "Back")]

    def __init__(self, track_ids: list[int]) -> None:
        super().__init__()
        self._track_ids = track_ids
        self._preset_values: PresetValues | None = None
        self._anomalies: AnomalyMatrix | None = None

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical():
            yield Static("Computing statistics…", id="status")
            yield DataTable(id="report-table")
            yield Static("", id="summary-stats")
            with Vertical(id="export-form"):
                yield Label("Preset name:", classes="field-label")
                yield Input(placeholder="e.g. Heavy Rock 2000s", id="preset-name")
                yield Label("Description (optional):", classes="field-label")
                yield Input(placeholder="one-line genre characterisation", id="preset-desc")
                yield Static("", id="overwrite-warning")
                yield Button(
                    "Export preset", id="export-btn", variant="success", disabled=True
                )
        yield Footer()

    def on_mount(self) -> None:
        self._build_report()

    def _build_report(self) -> None:
        db = self.app.db  # type: ignore[attr-defined]
        analyses = db.get_analyses_for_tracks(self._track_ids)

        if len(analyses) < 2:
            self.query_one("#status", Static).update(
                "[red]Not enough analyzed tracks (need ≥ 2)[/red]"
            )
            return

        self._preset_values = compute_preset(analyses)
        self._anomalies = compute_anomalies(analyses)

        table = self.query_one("#report-table", DataTable)
        table.clear(columns=True)

        # Build column list
        cols = ["Artist", "Title", "Genre", "Mood"] + _BAND_COLS + _SCALAR_COLS
        table.add_columns(*cols)

        # One row per track
        for idx, tid in enumerate(self._anomalies.track_ids):
            meta = db.get_metadata(tid)
            artist = (meta["artist"] or "–") if meta else "–"
            title  = (meta["title"]  or "–") if meta else "–"
            genre  = (meta["genre"]  or "–") if meta else "–"
            mood   = (meta["mood"]   or "–") if meta else "–"

            row_cells: list[str] = [artist, title, genre, mood]

            # Band RMS
            a = analyses[idx]
            for bi in range(NUM_BANDS):
                z = self._anomalies.band_rms_z[idx][bi]
                val = a["band_rms_db"][bi]
                row_cells.append(_colored(f"{val:.1f}", z))

            # Band correlation
            for bi in range(NUM_BANDS):
                z = self._anomalies.band_corr_z[idx][bi]
                val = a["band_correlation"][bi]
                row_cells.append(_colored(f"{val:.3f}", z))

            # Band transient
            for bi in range(NUM_BANDS):
                z = self._anomalies.band_transient_z[idx][bi]
                val = a["band_transient_db"][bi]
                row_cells.append(_colored(f"{val:.1f}", z))

            # Scalars
            z_ov = self._anomalies.overall_rms_z[idx]
            z_oc = self._anomalies.overall_corr_z[idx]
            row_cells.append(_colored(f"{a['overall_rms_db']:.1f}", z_ov))
            row_cells.append(_colored(f"{a['overall_correlation']:.3f}", z_oc))

            table.add_row(*row_cells, key=str(tid))

        # Summary row with computed preset values
        pv = self._preset_values
        summary_cells = ["[bold]PRESET[/bold]", "computed values", "", ""]
        for v in pv.band_rms_db:       summary_cells.append(f"[bold]{v:.1f}[/bold]")
        for v in pv.band_min_corr:     summary_cells.append(f"[bold]{v:.3f}[/bold]")
        for v in pv.band_transient_db: summary_cells.append(f"[bold]{v:.1f}[/bold]")
        summary_cells.append(f"[bold]{pv.overall_rms_db:.1f}[/bold]")
        summary_cells.append(f"[bold]{pv.overall_min_corr:.3f}[/bold]")
        table.add_row(*summary_cells, key="__preset__")

        self.query_one("#status", Static).update(
            f"[green]{len(analyses)} tracks analysed.[/green] "
            "[yellow]Yellow[/yellow] = mild outlier (|z|≥1.5) · "
            "[red]Red[/red] = strong outlier (|z|≥2.5)"
        )

    @on(Input.Changed, "#preset-name")
    def on_name_changed(self, event: Input.Changed) -> None:
        name = event.value.strip()
        btn = self.query_one("#export-btn", Button)
        warning = self.query_one("#overwrite-warning", Static)
        if not name:
            btn.disabled = True
            warning.update("")
            return
        btn.disabled = False
        db = self.app.db  # type: ignore[attr-defined]
        xml_exists = preset_xml_path(name).exists()
        db_exists = db.preset_exists(name)
        if xml_exists or db_exists:
            warning.update("[yellow]⚠ A preset with this name already exists — export will overwrite it.[/yellow]")
        else:
            warning.update("")

    @on(Button.Pressed, "#export-btn")
    def on_export(self) -> None:
        if self._preset_values is None:
            return
        name = self.query_one("#preset-name", Input).value.strip()
        desc = self.query_one("#preset-desc", Input).value.strip()
        if not name:
            self.notify("Enter a preset name", severity="warning")
            return

        try:
            out = export_preset(name, desc, self._preset_values)
            db = self.app.db  # type: ignore[attr-defined]
            db.save_preset(name, desc, str(out), self._track_ids)
            self.notify(
                f"Preset '{name}' exported to {out}", severity="information"
            )
            self.app.pop_screen()
        except Exception as exc:
            self.notify(f"Export failed: {exc}", severity="error")


def _colored(text: str, z: float) -> str:
    color = anomaly_color(z)
    if color == "default":
        return text
    return f"[{color}]{text}[/{color}]"
