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
                Button("Ingest audio files",  id="ingest",  variant="primary"),
                Button("Browse & tag tracks", id="browse",  variant="default"),
                Button("Create preset",       id="create",  variant="success"),
                Button("Quit",                id="quit",    variant="error"),
            )
        )
        yield Footer()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "ingest":
            self.app.push_screen("ingest")
        elif event.button.id == "browse":
            self.app.push_screen("browse")
        elif event.button.id == "create":
            self.app.push_screen("create_preset")
        elif event.button.id == "quit":
            self.app.exit()
