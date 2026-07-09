"""Logging primitives shared by the GUI and CLI."""

from __future__ import annotations

from collections.abc import Callable


class LogBuffer:
    def __init__(self, callback: Callable[[str], None] | None = None):
        self.lines: list[str] = []
        self.callback = callback

    def info(self, message: str) -> None:
        self.write(message)

    def write(self, message: str) -> None:
        text = str(message).rstrip("\n")
        if not text:
            return
        for line in text.splitlines():
            self.lines.append(line)
            if self.callback:
                self.callback(line)

    def text(self) -> str:
        return "\n".join(self.lines)
