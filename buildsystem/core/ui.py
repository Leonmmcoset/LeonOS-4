from __future__ import annotations

import curses
import os
import sys
import tomllib
from pathlib import Path

from .model import BuildGraph
from .runner import BuildSettings
from .state import BuildPaths


def load_settings(paths: BuildPaths) -> BuildSettings:
    defaults = BuildSettings.automatic()
    if not paths.settings.exists():
        save_settings(paths, defaults)
        return defaults
    try:
        raw = tomllib.loads(paths.settings.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError):
        return defaults
    build = raw.get("build", {})
    if not isinstance(build, dict):
        return defaults
    return BuildSettings(
        worker_threads=max(1, int(build.get("worker_threads", defaults.worker_threads))),
        max_processes=max(1, int(build.get("max_processes", defaults.max_processes))),
        download_retries=max(0, int(build.get("download_retries", defaults.download_retries))),
    )


def save_settings(paths: BuildPaths, settings: BuildSettings) -> None:
    paths.config.mkdir(parents=True, exist_ok=True)
    paths.settings.write_text(
        "[build]\n"
        f"worker_threads = {settings.worker_threads}\n"
        f"max_processes = {settings.max_processes}\n"
        f"download_retries = {settings.download_retries}\n",
        encoding="utf-8",
        newline="\n",
    )


def require_tty() -> None:
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        raise RuntimeError("this command requires an interactive TTY")


def edit_settings(paths: BuildPaths) -> None:
    require_tty()
    settings = load_settings(paths)
    values = [settings.worker_threads, settings.max_processes, settings.download_retries]
    labels = ["Worker threads", "External processes", "Download retries"]
    selected = 0

    def draw(screen: curses.window) -> None:
        nonlocal selected
        curses.curs_set(0)
        screen.keypad(True)
        while True:
            screen.erase()
            rows, cols = screen.getmaxyx()
            screen.addnstr(1, 2, "LeonOS BuildSystem settings", cols - 4, curses.A_BOLD)
            screen.addnstr(3, 2, "Up/Down: select   Left/Right: change   Enter: save   q: cancel", cols - 4)
            screen.addnstr(4, 2, f"Logical CPUs detected: {max(1, os.cpu_count() or 1)}", cols - 4)
            for index, label in enumerate(labels):
                attribute = curses.A_REVERSE if index == selected else curses.A_NORMAL
                screen.addnstr(6 + index * 2, 4, f"{label:<22} {values[index]}", cols - 8, attribute)
            screen.refresh()
            key = screen.getch()
            if key in (curses.KEY_UP, ord("k")):
                selected = (selected - 1) % len(values)
            elif key in (curses.KEY_DOWN, ord("j")):
                selected = (selected + 1) % len(values)
            elif key in (curses.KEY_LEFT, ord("h")):
                values[selected] = max(0 if selected == 2 else 1, values[selected] - 1)
            elif key in (curses.KEY_RIGHT, ord("l")):
                values[selected] += 1
            elif key in (10, 13, curses.KEY_ENTER):
                save_settings(
                    paths,
                    BuildSettings(
                        worker_threads=max(1, values[0]),
                        max_processes=max(1, values[1]),
                        download_retries=max(0, values[2]),
                    ),
                )
                return
            elif key in (ord("q"), 27):
                return

    curses.wrapper(draw)


def show_map(graph: BuildGraph) -> None:
    require_tty()
    lines = graph.map_lines()

    def draw(screen: curses.window) -> None:
        offset = 0
        curses.curs_set(0)
        screen.keypad(True)
        while True:
            screen.erase()
            rows, cols = screen.getmaxyx()
            screen.addnstr(0, 2, "LeonOS BuildSystem dependency map  (q to close)", cols - 4, curses.A_BOLD)
            visible = max(1, rows - 2)
            for index, line in enumerate(lines[offset : offset + visible]):
                screen.addnstr(index + 1, 1, line, cols - 2)
            screen.refresh()
            key = screen.getch()
            if key in (ord("q"), 27):
                return
            if key in (curses.KEY_UP, ord("k")):
                offset = max(0, offset - 1)
            elif key in (curses.KEY_DOWN, ord("j")):
                offset = min(max(0, len(lines) - visible), offset + 1)
            elif key == curses.KEY_NPAGE:
                offset = min(max(0, len(lines) - visible), offset + visible)
            elif key == curses.KEY_PPAGE:
                offset = max(0, offset - visible)

    curses.wrapper(draw)
