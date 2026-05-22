"""Main Textual application."""
from __future__ import annotations

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

#progress-label {
    color: #6666aa;
    height: 1;
}

#summary-label {
    color: #44aacc;
    height: 1;
    margin-top: 1;
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

    def on_mount(self) -> None:
        self.push_screen("main")
