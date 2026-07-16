from textual.app import ComposeResult
from textual.screen import Screen
from textual.widgets import Button, Footer, Header, Label
from textual.containers import Center, Middle


class MainMenuScreen(Screen):
    BINDINGS = [("q", "app.quit", "Quit")]

    def compose(self) -> ComposeResult:
        yield Header()
        yield Middle(
            Center(
                Label("MixAdvice Preset Builder", id="title"),
                Button("Ingest audio files (disabled)",  id="ingest",  variant="primary", disabled=True),
                Button("Browse & tag tracks",             id="browse",  variant="default"),
                Button("Create preset (disabled)",        id="create",  variant="success", disabled=True),
                Button("Quit",                            id="quit",    variant="error"),
                Label(
                    "Ingest and Create preset are frozen — this tool's analysis\n"
                    "hasn't been updated yet to match Codex/MasterTweak's preset\n"
                    "builder. Use Codex's Qt6 preset builder to ingest tracks and\n"
                    "build/export new presets for now. Browse still works read-only\n"
                    "against the existing local database.",
                    id="deprecation-notice",
                ),
            )
        )
        yield Footer()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "browse":
            self.app.push_screen("browse")
        elif event.button.id == "quit":
            self.app.exit()
