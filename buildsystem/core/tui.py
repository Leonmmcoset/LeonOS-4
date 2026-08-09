from __future__ import annotations

import curses
import json
import queue
import re
import shlex
import subprocess
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Callable

from .model import BuildGraph, Target
from .runner import BuildFailure, BuildSettings
from .state import BuildPaths, TaskStore, strip_ansi
from .ui import load_settings, save_settings


TEST_ITEMS = (
    "license-server",
    "los2w",
    "qmp-terminal",
    "qmp-pleditor",
    "qmp-tcc",
    "qmp-stardust",
    "all",
)

BUILD_PRESETS = (
    ("all", "Complete OS image"),
    ("kernel", "Kernel"),
    ("loader", "Boot loader"),
    ("drivers", "Drivers"),
    ("middlelayer", "Middle layer"),
    ("userland", "User applications"),
    ("image-vmdk", "VMDK disk image"),
    ("image-iso", "ISO image"),
    ("installer", "Installer ISO"),
    ("clean", "Clean generated files"),
    ("menuconfig", "Edit build configuration"),
    ("run", "Run in QEMU"),
    ("run-debug", "Debug QEMU"),
    ("run-iso", "Run ISO in QEMU"),
)

TASK_ID_RE = re.compile(r"\((\d{9})\)")
LOG_TAIL_BYTES = 256 * 1024
MAX_LOG_LINES = 12_000
TASK_REFRESH_SECONDS = 0.75
TASK_IDLE_REFRESH_SECONDS = 2.5
UI_REDRAW_SECONDS = 0.12
INPUT_BATCH_LIMIT = 64
IDLE_SLEEP_SECONDS = 0.006

GraphLoader = Callable[[], BuildGraph]


class CommandJob:
    """A build.py child process whose output is consumed by the curses loop."""

    def __init__(self, command: list[str], args: list[str], root: Path) -> None:
        self.command = command
        self.args = args
        self.output: queue.SimpleQueue[str] = queue.SimpleQueue()
        self.collected: list[str] = []
        self.task_id: str | None = None
        self.process = subprocess.Popen(
            command,
            cwd=root,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.reader_done = threading.Event()
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()

    def _read_output(self) -> None:
        assert self.process.stdout is not None
        for raw in self.process.stdout:
            line = strip_ansi(raw.rstrip("\r\n"))
            match = TASK_ID_RE.search(line)
            if match is not None:
                self.task_id = match.group(1)
            self.collected.append(line)
            self.output.put(line)
        self.reader_done.set()

    def drain(self) -> list[str]:
        lines: list[str] = []
        while True:
            try:
                lines.append(self.output.get_nowait())
            except queue.Empty:
                return lines


class BuildTui:
    def __init__(
        self,
        paths: BuildPaths,
        graph_loader: GraphLoader,
        graph: BuildGraph | None = None,
    ) -> None:
        self.paths = paths
        self.graph_loader = graph_loader
        self.graph = graph
        self.store = TaskStore(paths)
        self.screen: curses.window | None = None
        self.running = True
        self.exit_code = 0
        self.started_at = time.monotonic()
        self.view = "main"
        self.previous_view = "main"
        self.selected = 0
        self.build_selected = 0
        self.preset_scroll = 0
        self.filter_text = ""
        self.main_scroll = 0
        self.text_scroll = 0
        self.task_selected = 0
        self.settings_selected = 0
        self.settings_values: list[int] = []
        self.action_selected = 0
        self.test_selected = 0
        self.viewer_title = ""
        self.viewer_lines: list[str] = []
        self.output_lines: deque[str] = deque(maxlen=MAX_LOG_LINES)
        self.output_title = ""
        self.output_follow = False
        self.job: CommandJob | None = None
        self.status_message = "Ready"
        self.observed_task_id: str | None = None
        self.runtime_record: dict[str, object] = {}
        self.runtime_log_lines: list[str] = []
        self.runtime_current: list[str] = []
        self._recent_records: list[dict[str, object]] = []
        self._task_records_cache: list[dict[str, object]] = []
        self._task_records_cache_at = 0.0
        self._log_cache: dict[str, tuple[int, int, list[str]]] = {}
        self._filtered_cache_key: tuple[int, str, int] | None = None
        self._filtered_cache: list[str] = []
        self._target_detail_cache: dict[tuple[int, str], list[str]] = {}
        self.runtime_updates: queue.SimpleQueue[tuple[str | None, dict[str, object], list[str], list[str]]] = queue.SimpleQueue()
        self.runtime_stop = threading.Event()
        self.runtime_thread: threading.Thread | None = None
        self.dirty = True
        self.graph_state = "ready" if graph is not None else "not_started"
        self.graph_error = ""
        self.graph_thread: threading.Thread | None = None
        self.graph_lock = threading.Lock()
        self._colors = False

    def run(self, screen: curses.window) -> int:
        self.screen = screen
        screen.keypad(True)
        screen.nodelay(True)
        try:
            curses.curs_set(0)
        except curses.error:
            pass
        self._init_colors()
        self._start_runtime_watcher()
        next_draw = 0.0
        try:
            while self.running:
                self._drain_job()
                if self._drain_runtime_updates():
                    self.dirty = True
                handled_keys = self._drain_keys()
                if handled_keys:
                    self.dirty = True
                now = time.monotonic()
                if self.dirty or (self._needs_periodic_draw() and now >= next_draw):
                    self._draw()
                    self.dirty = False
                    next_draw = now + UI_REDRAW_SECONDS
                if not handled_keys:
                    time.sleep(IDLE_SLEEP_SECONDS)
        finally:
            self.runtime_stop.set()
        return self.exit_code

    def _drain_keys(self) -> int:
        assert self.screen is not None
        handled = 0
        while handled < INPUT_BATCH_LIMIT and self.running:
            key = self.screen.getch()
            if key == -1:
                break
            self._handle_key(key)
            handled += 1
        return handled

    def _init_colors(self) -> None:
        try:
            if curses.has_colors():
                curses.start_color()
                curses.use_default_colors()
                curses.init_pair(1, curses.COLOR_CYAN, -1)
                curses.init_pair(2, curses.COLOR_GREEN, -1)
                curses.init_pair(3, curses.COLOR_YELLOW, -1)
                curses.init_pair(4, curses.COLOR_RED, -1)
                curses.init_pair(5, curses.COLOR_BLUE, -1)
                curses.init_pair(6, curses.COLOR_MAGENTA, -1)
                curses.init_pair(7, curses.COLOR_WHITE, curses.COLOR_BLUE)
                self._colors = True
        except curses.error:
            self._colors = False

    def _color(self, index: int) -> int:
        return curses.color_pair(index) if self._colors else 0

    def _needs_periodic_draw(self) -> bool:
        if self.graph_state == "loading":
            return True
        if self.job is not None:
            return True
        status = str(self.runtime_record.get("status", ""))
        return self.view in {"main", "monitor", "output"} and status in {"queued", "running"}

    def _start_runtime_watcher(self) -> None:
        if self.runtime_thread is not None:
            return
        self.runtime_thread = threading.Thread(target=self._runtime_worker, daemon=True)
        self.runtime_thread.start()

    def _runtime_worker(self) -> None:
        while not self.runtime_stop.is_set():
            observed, record, lines, current = self._snapshot_runtime()
            self.runtime_updates.put((observed, record, lines, current))
            status = str(record.get("status", ""))
            interval = TASK_REFRESH_SECONDS if status in {"queued", "running"} else TASK_IDLE_REFRESH_SECONDS
            self.runtime_stop.wait(interval)

    def _drain_runtime_updates(self) -> bool:
        changed = False
        while True:
            try:
                observed, record, lines, current = self.runtime_updates.get_nowait()
            except queue.Empty:
                return changed
            if observed is not None and observed != self.observed_task_id:
                self.observed_task_id = observed
                changed = True
            if record != self.runtime_record:
                self.runtime_record = record
                changed = True
            if len(lines) != len(self.runtime_log_lines) or (lines[-1:] != self.runtime_log_lines[-1:]):
                self.runtime_log_lines = lines
                changed = True
            if current != self.runtime_current:
                self.runtime_current = current
                changed = True

    def _snapshot_runtime(self) -> tuple[str | None, dict[str, object], list[str], list[str]]:
        records = self._read_task_records(force=True)
        active = [
            record for record in records
            if str(record.get("status", "")) in {"queued", "running"}
        ]
        observed = self.observed_task_id
        observed_record = next(
            (record for record in records if str(record.get("id", "")) == observed),
            None,
        )
        if active and (
            observed is None
            or observed_record is None
            or str(observed_record.get("status", "")) not in {"queued", "running"}
        ):
            observed = str(active[0].get("id", ""))
        if observed is None:
            return None, {}, [], []
        selected = next(
            (record for record in records if str(record.get("id", "")) == observed),
            None,
        )
        if selected is None:
            return observed, {}, [], []
        lines = self._task_log_lines(selected)
        return observed, selected, lines, self._current_log_activity(lines)

    def _start_graph_load(self) -> None:
        with self.graph_lock:
            if self.graph_state in {"loading", "ready"}:
                return
            self.graph_state = "loading"
            self.graph_error = ""
        self.graph_thread = threading.Thread(target=self._load_graph_worker, daemon=True)
        self.graph_thread.start()

    def _load_graph_worker(self) -> None:
        try:
            graph = self.graph_loader()
        except BaseException as error:
            with self.graph_lock:
                self.graph = None
                self.graph_state = "failed"
                self.graph_error = str(error) or repr(error)
            return
        with self.graph_lock:
            self.graph = graph
            self.graph_state = "ready"
            self.graph_error = ""

    def _graph(self) -> BuildGraph | None:
        if self.graph is not None:
            return self.graph
        self._start_graph_load()
        return None

    def _graph_status(self) -> str:
        if self.graph_state == "ready" and self.graph is not None:
            return f"{len(self.graph.targets)} targets loaded"
        if self.graph_state == "loading":
            spinner = "|/-\\"[int((time.monotonic() - self.started_at) * 8) % 4]
            return f"loading graph {spinner}"
        if self.graph_state == "failed":
            return "graph load failed"
        return "graph ready on demand"

    def _drain_job(self) -> None:
        if self.job is None:
            return
        changed = False
        for line in self.job.drain():
            self.output_lines.append(line)
            changed = True
        if self.job.task_id is not None:
            self.observed_task_id = self.job.task_id
        returncode = self.job.process.poll()
        if returncode is None or not self.job.reader_done.is_set():
            if changed:
                self.dirty = True
            return
        title = self.output_title
        if returncode == 0:
            self.output_lines.append("")
            self.output_lines.append(f"[completed] {title}")
            self.status_message = f"{title} completed"
        else:
            self.output_lines.append("")
            self.output_lines.append(f"[failed: exit {returncode}] {title}")
            self.status_message = f"{title} failed with exit {returncode}"
            self.exit_code = 1
        if self.job.args and self.job.args[0] == "client" and returncode == 0:
            self._note_queued_task(self.job.collected)
            self.view = "main"
        self.job = None
        self.dirty = True

    def _note_queued_task(self, lines: list[str]) -> None:
        try:
            value = json.loads("\n".join(lines))
        except json.JSONDecodeError:
            return
        if isinstance(value, dict) and isinstance(value.get("task_id"), str):
            self.observed_task_id = value["task_id"]
            self.status_message = f"Queued background task {value['task_id']}"

    def _read_task_records(self, *, force: bool = False) -> list[dict[str, object]]:
        now = time.monotonic()
        if not force and now - self._task_records_cache_at < TASK_REFRESH_SECONDS:
            return list(self._task_records_cache)
        records: list[dict[str, object]] = []
        if not self.paths.tasks.exists():
            return records
        for path in self.paths.tasks.glob("*.json"):
            try:
                value = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            if isinstance(value, dict):
                records.append(value)
        self._task_records_cache = sorted(
            records,
            key=lambda value: str(value.get("created_at", "")),
            reverse=True,
        )
        self._task_records_cache_at = now
        return list(self._task_records_cache)

    def _task_log_lines(self, record: dict[str, object]) -> list[str]:
        raw_path = record.get("log")
        if not isinstance(raw_path, str):
            return []
        task_id = str(record.get("id", raw_path))
        path = (self.paths.root / raw_path).resolve()
        try:
            path.relative_to(self.paths.root.resolve())
            stat = path.stat()
            cached = self._log_cache.get(task_id)
            if cached is not None and cached[0] == stat.st_mtime_ns and cached[1] == stat.st_size:
                return cached[2]
            with path.open("rb") as handle:
                if stat.st_size > LOG_TAIL_BYTES:
                    handle.seek(-LOG_TAIL_BYTES, 2)
                    handle.readline()
                raw = handle.read()
            lines = [
                strip_ansi(line)
                for line in raw.decode("utf-8", errors="replace").splitlines()
            ][-MAX_LOG_LINES:]
            self._log_cache[task_id] = (stat.st_mtime_ns, stat.st_size, lines)
            return lines
        except (OSError, ValueError):
            return []

    def _current_log_activity(self, lines: list[str]) -> list[str]:
        activity: list[str] = []
        for line in lines[-120:]:
            if "Building " in line or "Generating " in line or "Running command:" in line:
                activity.append(line.strip())
        return activity[-4:]

    def _draw(self) -> None:
        assert self.screen is not None
        self.screen.erase()
        rows, cols = self.screen.getmaxyx()
        if rows < 12 or cols < 54:
            self._add(1, 2, "LeonOS BuildSystem TUI needs a terminal of at least 54x12.")
            self._add(3, 2, "Resize the terminal, then press q to exit.", self._color(3))
            self.screen.refresh()
            return
        self._draw_header(cols)
        if self.view == "main":
            self._draw_dashboard(rows, cols)
        elif self.view == "targets":
            self._draw_targets(rows, cols)
        elif self.view == "viewer":
            self._draw_text_view(rows, cols, self.viewer_title, self.viewer_lines)
        elif self.view == "output":
            title = self.output_title
            if self.job is not None:
                title += "  [running]"
            title += "  [following]" if self.output_follow else "  [scroll paused]"
            progress = self._progress_summary()
            if progress:
                title += f"  |  {progress}"
            self._draw_text_view(rows, cols, title, list(self.output_lines), follow=self.output_follow)
        elif self.view == "monitor":
            self._draw_monitor(rows, cols)
        elif self.view == "tasks":
            self._draw_tasks(rows, cols)
        elif self.view == "settings":
            self._draw_settings(cols)
        elif self.view == "actions":
            self._draw_actions(cols)
        elif self.view == "tests":
            self._draw_tests(cols)
        self._draw_footer(rows, cols)
        self.screen.refresh()

    def _draw_header(self, cols: int) -> None:
        job_state = "idle" if self.job is None else "running"
        view_name = {
            "main": "Dashboard",
            "targets": "Target explorer",
            "tasks": "Task history",
            "monitor": "Live monitor",
        }.get(self.view, self.view.title())
        right = f" {view_name} | {job_state} | {self._graph_status()} "
        header_attr = curses.A_REVERSE | (self._color(7) if self._colors else 0)
        self._add(0, 0, " " * cols, header_attr, cols)
        self._add(0, 1, " LeonOS Build Console ", curses.A_BOLD | header_attr, cols - 2)
        self._add(0, max(1, cols - len(right) - 1), right, header_attr, len(right))
        self._add(1, 1, self.status_message, self._color(3), cols - 2)

    def _draw_footer(self, rows: int, cols: int) -> None:
        if self.view == "main":
            text = "Up/Down choose build  Enter start  b background  o observe  e targets  t tasks  c cache  s settings  : commands  ? help  q exit"
        elif self.view == "targets":
            text = "Up/Down select  Enter actions  / filter  r run  b queue  p profile  i info  w why  g gen  q back"
        elif self.view == "tasks":
            text = "Up/Down select  Enter/l log  i status  r refresh  q back"
        elif self.view == "settings":
            text = "Up/Down select  Left/Right change  Enter save  q cancel"
        elif self.view in {"actions", "tests"}:
            text = "Up/Down select  Enter choose  q cancel"
        elif self.view == "monitor":
            text = "Up/Down scroll log  PageUp/PageDown jump  g/G top/end  q dashboard"
        elif self.view == "output":
            state = "pause follow" if self.output_follow else "resume follow"
            text = f"Up/Down scroll  PageUp/PageDown jump  g/G top/end  f {state}  q back"
        else:
            text = "Up/Down scroll  PageUp/PageDown jump  q back"
        self._add(rows - 1, 1, text, curses.A_REVERSE, cols - 2)

    def _draw_dashboard(self, rows: int, cols: int) -> None:
        presets = self._available_presets()
        if presets:
            self.build_selected = min(self.build_selected, len(presets) - 1)
        self._add(2, 1, "Dashboard", curses.A_BOLD | self._color(1), 12)
        self._add(
            2,
            14,
            "[Enter] Start   [B] Background   [O] Observe   [E] Explorer   [:] Command",
            self._color(3),
            cols - 16,
        )
        top = 4
        panel_height = max(9, min(13, rows // 3 + 1))
        left_width = min(42, max(32, cols // 3))
        right_left = left_width + 2
        right_width = cols - right_left - 2
        self._box(top, 1, left_width, panel_height, "Quick Builds")
        self._box(top, right_left, right_width, panel_height, "Active Task")
        preset_visible = max(1, panel_height - 3)
        if self.build_selected < self.preset_scroll:
            self.preset_scroll = self.build_selected
        if self.build_selected >= self.preset_scroll + preset_visible:
            self.preset_scroll = self.build_selected - preset_visible + 1
        for index, (name, label) in enumerate(
            presets[self.preset_scroll : self.preset_scroll + preset_visible]
        ):
            preset_index = self.preset_scroll + index
            attribute = curses.A_REVERSE if preset_index == self.build_selected else curses.A_NORMAL
            kind = self._preset_kind(name)
            self._add(top + 2 + index, 3, f" {name:<13} {label:<22} {kind:>6}", attribute, left_width - 4)
        if len(presets) > preset_visible:
            self._add(top + panel_height - 1, 3, f"{self.preset_scroll + 1}-{min(len(presets), self.preset_scroll + preset_visible)} / {len(presets)}", self._color(3), left_width - 4)
        self._draw_runtime_panel(top + 1, right_left + 1, right_width - 2, panel_height - 2)

        log_top = top + panel_height + 1
        log_height = max(5, rows - log_top - 3)
        self._box(log_top, 1, cols - 2, log_height, "Activity Stream")
        lines = self.runtime_log_lines[-max(1, log_height - 2) :]
        if self.job is not None:
            lines = list(self.output_lines)[-max(1, log_height - 2) :]
        if not lines:
            lines = ["No build is running. Choose a build above and press Enter."]
        for index, line in enumerate(lines):
            self._add(log_top + 1 + index, 3, line, 0, cols - 6)

    def _draw_targets(self, rows: int, cols: int) -> None:
        graph = self._graph()
        if graph is None:
            message = "Loading target graph in the background..."
            if self.graph_state == "failed":
                message = f"Unable to load target graph: {self.graph_error}"
            self._add(4, 3, message, self._color(3), cols - 6)
            self._add(6, 3, "Build controls remain available from Dashboard.", 0, cols - 6)
            return
        names = self._filtered_targets()
        if not names:
            self._add(3, 2, "No target matches the current filter.", self._color(3))
            return
        self.selected = min(self.selected, len(names) - 1)
        target = graph.targets[names[self.selected]]
        left_width = min(max(27, cols // 3), cols - 30)
        body_top = 3
        body_bottom = rows - 2
        visible = max(1, body_bottom - body_top)
        if self.selected < self.main_scroll:
            self.main_scroll = self.selected
        if self.selected >= self.main_scroll + visible:
            self.main_scroll = self.selected - visible + 1
        self._add(2, 1, f"Targets ({len(names)})  filter: {self.filter_text or '(none)'}", curses.A_BOLD)
        self._add(2, left_width + 2, "Selected target", curses.A_BOLD)
        for index, name in enumerate(names[self.main_scroll : self.main_scroll + visible]):
            target_index = self.main_scroll + index
            candidate = graph.targets[name]
            marker = ">" if target_index == self.selected else " "
            label = f"{marker} {candidate.name:<28} {candidate.kind}"
            attribute = curses.A_REVERSE if target_index == self.selected else curses.A_NORMAL
            self._add(body_top + index, 1, label, attribute, left_width - 1)
        for y in range(body_top - 1, body_bottom):
            self._add(y, left_width, "|", self._color(1), 1)
        detail_width = cols - left_width - 3
        for offset, line in enumerate(self._target_details(graph, target)):
            if body_top + offset >= body_bottom:
                break
            self._add(body_top + offset, left_width + 2, line, 0, detail_width)

    def _draw_runtime_panel(self, top: int, left: int, width: int, height: int) -> None:
        record = self.runtime_record
        if not record:
            if self.job is not None:
                self._add(top, left, "STARTING", curses.A_BOLD | self._color(3), width)
                self._add(top + 1, left, self.output_title or "build.py command", 0, width)
                self._draw_progress_bar(top + 3, left, width, 0)
                self._add(top + 5, left, "Preparing build graph and tool checks", 0, width)
                return
            self._add(top, left, "IDLE", curses.A_BOLD | self._color(2), width)
            self._add(top + 1, left, "No build is currently running.", 0, width)
            self._draw_progress_bar(top + 3, left, width, 0)
            self._add(top + 5, left, f"Graph: {self._graph_status()}", self._color(3), width)
            return
        status = str(record.get("status", "unknown"))
        task = str(record.get("task", "build"))
        task_id = str(record.get("id", self.observed_task_id or "?"))
        completed = self._as_int(record.get("completed_targets"))
        total = self._as_int(record.get("total_targets"))
        percent = self._as_int(record.get("progress_percent"))
        if percent <= 0 and total > 0:
            percent = int(completed * 100 / total)
        color = self._color(2) if status == "done" else self._color(4) if status == "failed" else self._color(3)
        self._add(top, left, f"{status.upper():<8} {task}", curses.A_BOLD | color, width)
        self._add(top + 1, left, f"Task ID {task_id}    Targets {completed}/{total or '?'}", 0, width)
        elapsed = record.get("elapsed_seconds")
        if elapsed is not None:
            self._add(top + 2, left, f"Elapsed {elapsed}s", 0, width)
        else:
            self._add(top + 2, left, f"Updated {record.get('updated_at', record.get('started_at', ''))}", 0, width)
        self._draw_progress_bar(top + 3, left, width, percent)
        raw_running = record.get("running_targets", [])
        running = raw_running if isinstance(raw_running, list) else []
        current = self.runtime_current or [str(item) for item in running]
        self._add(top + 4, left, "Current work", curses.A_BOLD, width)
        if current:
            for index, item in enumerate(current[: max(1, height - 6)]):
                self._add(top + 5 + index, left, f"- {item}", 0, width)
        else:
            self._add(top + 5, left, "(scheduler is between targets)", 0, width)

    def _draw_progress_bar(self, row: int, left: int, width: int, percent: int) -> None:
        bar_width = max(8, width - 8)
        fill = int(bar_width * min(100, max(0, percent)) / 100)
        bar = "#" * fill + "-" * (bar_width - fill)
        self._add(row, left, f"[{bar}] {percent:3d}%", self._color(2), width)

    def _progress_summary(self) -> str:
        if not self.runtime_record:
            return ""
        completed = self._as_int(self.runtime_record.get("completed_targets"))
        total = self._as_int(self.runtime_record.get("total_targets"))
        percent = self._as_int(self.runtime_record.get("progress_percent"))
        return f"{percent}% ({completed}/{total or '?'} targets)"

    def _draw_monitor(self, rows: int, cols: int) -> None:
        top = 3
        panel_height = 7
        self._box(top, 1, cols - 2, panel_height, "Observed build")
        self._draw_runtime_panel(top + 1, 3, cols - 6, panel_height - 2)
        log_top = top + panel_height + 1
        log_height = max(5, rows - log_top - 3)
        self._box(log_top, 1, cols - 2, log_height, "Task log")
        visible = max(1, log_height - 2)
        max_offset = max(0, len(self.runtime_log_lines) - visible)
        self.text_scroll = min(self.text_scroll, max_offset)
        for index, line in enumerate(self.runtime_log_lines[self.text_scroll : self.text_scroll + visible]):
            self._add(log_top + 1 + index, 3, line, 0, cols - 6)

    def _target_details(self, graph: BuildGraph, target: Target) -> list[str]:
        cache_key = (id(graph), target.name)
        cached = self._target_detail_cache.get(cache_key)
        if cached is not None:
            return cached
        lines = [
            f"Name: {target.name}",
            f"Kind: {target.kind}",
            f"Group target: {'yes' if target.group else 'no'}",
            f"Always run: {'yes' if target.always else 'no'}",
            "",
            "Outputs:",
        ]
        lines.extend(f"  {graph.relative(path)}" for path in target.outputs)
        if not target.outputs:
            lines.append("  (aggregate)")
        lines.append("")
        lines.append("Dependencies:")
        dependencies = graph.dependencies(target)
        lines.extend(f"  {item.name}" for item in dependencies)
        if not dependencies:
            lines.append("  (none)")
        inputs = target.all_inputs()
        if inputs:
            lines.append("")
            lines.append("Inputs:")
            lines.extend(f"  {graph.relative(path)}" for path in inputs[:8])
            remaining = len(inputs) - 8
            if remaining > 0:
                lines.append(f"  ... {remaining} more")
        state = self.store.read_target(target.name)
        if state:
            lines.append("")
            lines.append("Cached state:")
            lines.append(f"  outputs tracked: {len(state.get('outputs', []))}")
        self._target_detail_cache[cache_key] = lines
        return lines

    def _draw_text_view(
        self,
        rows: int,
        cols: int,
        title: str,
        lines: list[str],
        *,
        follow: bool = False,
    ) -> None:
        self._add(2, 1, title, curses.A_BOLD | self._color(1), cols - 2)
        visible = max(1, rows - 5)
        max_offset = max(0, len(lines) - visible)
        self.text_scroll = max_offset if follow else min(self.text_scroll, max_offset)
        for index, line in enumerate(lines[self.text_scroll : self.text_scroll + visible]):
            self._add(3 + index, 1, line, 0, cols - 2)
        footer = f"{self.text_scroll + 1}-{min(len(lines), self.text_scroll + visible)} / {len(lines)}"
        self._add(rows - 2, 1, footer, self._color(3), cols - 2)

    def _draw_tasks(self, rows: int, cols: int) -> None:
        self._add(2, 1, f"Recent build tasks ({len(self._recent_records)})", curses.A_BOLD | self._color(1))
        if not self._recent_records:
            self._add(4, 2, "No task records found.")
            return
        self.task_selected = min(self.task_selected, len(self._recent_records) - 1)
        visible = max(1, rows - 5)
        offset = min(max(0, self.task_selected - visible + 1), max(0, len(self._recent_records) - visible))
        for index, record in enumerate(self._recent_records[offset : offset + visible]):
            row = 3 + index
            record_index = offset + index
            task_id = str(record.get("id", "?"))
            status = str(record.get("status", "unknown"))
            task = str(record.get("task") or record.get("command") or "")
            created = str(record.get("created_at", ""))
            label = f"{task_id:<10} {status:<9} {created:<25} {task}"
            attribute = curses.A_REVERSE if record_index == self.task_selected else curses.A_NORMAL
            self._add(row, 1, label, attribute, cols - 2)

    def _draw_settings(self, cols: int) -> None:
        labels = ("Worker threads", "External processes", "Download retries")
        self._add(3, 2, "Build scheduler settings", curses.A_BOLD | self._color(1), cols - 4)
        for index, label in enumerate(labels):
            attribute = curses.A_REVERSE if index == self.settings_selected else curses.A_NORMAL
            value = self.settings_values[index]
            self._add(5 + index * 2, 4, f"{label:<22} {value}", attribute, cols - 8)

    def _draw_actions(self, cols: int) -> None:
        target = self._selected_target()
        choices = self._target_actions(target)
        self.action_selected = min(self.action_selected, len(choices) - 1)
        self._add(3, 2, f"Actions for {target.name}", curses.A_BOLD | self._color(1), cols - 4)
        for index, label in enumerate(choices):
            attribute = curses.A_REVERSE if index == self.action_selected else curses.A_NORMAL
            self._add(5 + index * 2, 4, label, attribute, cols - 8)

    def _draw_tests(self, cols: int) -> None:
        self._add(3, 2, "Select a test", curses.A_BOLD | self._color(1), cols - 4)
        for index, item in enumerate(TEST_ITEMS):
            attribute = curses.A_REVERSE if index == self.test_selected else curses.A_NORMAL
            self._add(5 + index * 2, 4, item, attribute, cols - 8)

    def _handle_key(self, key: int) -> None:
        if self.job is not None and key == 3:
            self.job.process.terminate()
            self.status_message = "Sent terminate request to active command"
            return
        if self.view == "main":
            self._handle_dashboard_key(key)
        elif self.view == "targets":
            self._handle_target_key(key)
        elif self.view in {"viewer", "output"}:
            self._handle_text_key(key)
        elif self.view == "monitor":
            self._handle_monitor_key(key)
        elif self.view == "tasks":
            self._handle_tasks_key(key)
        elif self.view == "settings":
            self._handle_settings_key(key)
        elif self.view == "actions":
            self._handle_actions_key(key)
        elif self.view == "tests":
            self._handle_tests_key(key)

    def _handle_dashboard_key(self, key: int) -> None:
        presets = self._available_presets()
        if key in (ord("q"), 27):
            self.running = False
        elif key in (curses.KEY_UP, ord("k")) and presets:
            self.build_selected = (self.build_selected - 1) % len(presets)
        elif key in (curses.KEY_DOWN, ord("j")) and presets:
            self.build_selected = (self.build_selected + 1) % len(presets)
        elif key in (10, 13, curses.KEY_ENTER) and presets:
            self._start_preset(background=False)
        elif key == ord("b") and presets:
            self._start_preset(background=True)
        elif key == ord("o") and self.observed_task_id:
            self.text_scroll = max(0, len(self.runtime_log_lines) - 1)
            self.view = "monitor"
        elif key == ord("e"):
            self.view = "targets"
        elif key == ord("t"):
            self._refresh_recent_tasks()
            self.view = "tasks"
        elif key == ord("c"):
            self._start_command(["cache", "stats"], "Cache statistics")
        elif key == ord("C"):
            if self._confirm("Prune stale cache state and temporary files?"):
                self._start_command(["cache", "prune"], "Cache prune")
        elif key == ord("s"):
            self._open_settings()
        elif key == ord("m"):
            self._open_dependency_map()
        elif key == ord("x"):
            self.view = "tests"
        elif key == ord(":"):
            value = self._prompt("build.py command")
            if value:
                self._execute_command(value)
        elif key == ord("?"):
            self._show_lines("TUI help", self._help_lines())

    def _handle_target_key(self, key: int) -> None:
        names = self._filtered_targets()
        if key in (ord("q"), 27):
            self.view = "main"
        elif key in (curses.KEY_UP, ord("k")) and names:
            self.selected = (self.selected - 1) % len(names)
        elif key in (curses.KEY_DOWN, ord("j")) and names:
            self.selected = (self.selected + 1) % len(names)
        elif key in (10, 13, curses.KEY_ENTER) and names:
            self.action_selected = 0
            self.view = "actions"
        elif key == ord("/"):
            value = self._prompt("Filter target names")
            if value is not None:
                self.filter_text = value.strip()
                self.selected = 0
                self.main_scroll = 0
        elif key == ord("r") and names:
            self._run_selected()
        elif key == ord("b") and names:
            self._queue_selected()
        elif key == ord("p") and names:
            self._start_command(["profile", self._selected_target().name], "Profile target")
        elif key == ord("i") and names:
            self._start_command(["info", self._selected_target().name], "Target information")
        elif key == ord("w") and names:
            self._start_command(["why", self._selected_target().name], "Rebuild explanation")
        elif key == ord("g") and names:
            self._start_command(["gen", self._selected_target().name], "Generate target")
        elif key == ord("m"):
            self._open_dependency_map()
        elif key == ord("t"):
            self._refresh_recent_tasks()
            self.view = "tasks"
        elif key == ord("s"):
            self._open_settings()
        elif key == ord("c"):
            self._start_command(["cache", "stats"], "Cache statistics")
        elif key == ord("C"):
            if self._confirm("Prune stale cache state and temporary files?"):
                self._start_command(["cache", "prune"], "Cache prune")
        elif key == ord("x"):
            self.view = "tests"
        elif key == ord("a"):
            value = self._prompt("File for affected query")
            if value:
                self._start_command(["affected", value], "Affected targets")
        elif key == ord(":"):
            value = self._prompt("build.py command")
            if value:
                self._execute_command(value)
        elif key == ord("?"):
            self._show_lines("TUI help", self._help_lines())

    def _handle_text_key(self, key: int) -> None:
        lines = self.viewer_lines if self.view == "viewer" else list(self.output_lines)
        rows, _ = self._size()
        visible = max(1, rows - 5)
        max_offset = max(0, len(lines) - visible)
        if key in (ord("q"), 27):
            if self.job is not None:
                self.status_message = "A command is still running; wait for it or press Ctrl-C first"
                return
            self.view = self.previous_view
            self.text_scroll = 0
        elif self.view == "output" and key == ord("f"):
            self.output_follow = not self.output_follow
            if self.output_follow:
                self.text_scroll = max_offset
        elif key in (curses.KEY_UP, ord("k")):
            self.text_scroll = max(0, self.text_scroll - 1)
            self._update_output_follow(max_offset)
        elif key in (curses.KEY_DOWN, ord("j")):
            self.text_scroll = min(max_offset, self.text_scroll + 1)
            self._update_output_follow(max_offset)
        elif key == curses.KEY_PPAGE:
            self.text_scroll = max(0, self.text_scroll - visible)
            self._update_output_follow(max_offset)
        elif key == curses.KEY_NPAGE:
            self.text_scroll = min(max_offset, self.text_scroll + visible)
            self._update_output_follow(max_offset)
        elif key in (ord("G"), curses.KEY_END):
            self.text_scroll = max_offset
            if self.view == "output":
                self.output_follow = True
        elif key in (ord("g"), curses.KEY_HOME):
            self.text_scroll = 0
            self._update_output_follow(max_offset)

    def _update_output_follow(self, max_offset: int) -> None:
        if self.view == "output":
            self.output_follow = self.text_scroll >= max_offset

    def _handle_monitor_key(self, key: int) -> None:
        rows, _ = self._size()
        visible = max(1, rows - 13)
        if key in (ord("q"), 27):
            self.view = "main"
            self.text_scroll = 0
        elif key in (curses.KEY_UP, ord("k")):
            self.text_scroll = max(0, self.text_scroll - 1)
        elif key in (curses.KEY_DOWN, ord("j")):
            self.text_scroll = min(max(0, len(self.runtime_log_lines) - visible), self.text_scroll + 1)
        elif key == curses.KEY_PPAGE:
            self.text_scroll = max(0, self.text_scroll - visible)
        elif key == curses.KEY_NPAGE:
            self.text_scroll = min(max(0, len(self.runtime_log_lines) - visible), self.text_scroll + visible)
        elif key == ord("G"):
            self.text_scroll = max(0, len(self.runtime_log_lines) - visible)
        elif key == ord("g"):
            self.text_scroll = 0

    def _handle_tasks_key(self, key: int) -> None:
        if key in (ord("q"), 27):
            self.view = "main"
        elif key in (curses.KEY_UP, ord("k")) and self._recent_records:
            self.task_selected = (self.task_selected - 1) % len(self._recent_records)
        elif key in (curses.KEY_DOWN, ord("j")) and self._recent_records:
            self.task_selected = (self.task_selected + 1) % len(self._recent_records)
        elif key == ord("r"):
            self._refresh_recent_tasks()
        elif key in (10, 13, curses.KEY_ENTER, ord("l")) and self._recent_records:
            self._open_task_log(self._selected_task_id())
        elif key == ord("i") and self._recent_records:
            self._show_task_status(self._selected_task_id())

    def _handle_settings_key(self, key: int) -> None:
        if key in (ord("q"), 27):
            self.view = "main"
        elif key in (curses.KEY_UP, ord("k")):
            self.settings_selected = (self.settings_selected - 1) % len(self.settings_values)
        elif key in (curses.KEY_DOWN, ord("j")):
            self.settings_selected = (self.settings_selected + 1) % len(self.settings_values)
        elif key in (curses.KEY_LEFT, ord("h")):
            minimum = 0 if self.settings_selected == 2 else 1
            self.settings_values[self.settings_selected] = max(
                minimum, self.settings_values[self.settings_selected] - 1
            )
        elif key in (curses.KEY_RIGHT, ord("l")):
            self.settings_values[self.settings_selected] += 1
        elif key in (10, 13, curses.KEY_ENTER):
            save_settings(
                self.paths,
                BuildSettings(
                    worker_threads=max(1, self.settings_values[0]),
                    max_processes=max(1, self.settings_values[1]),
                    download_retries=max(0, self.settings_values[2]),
                ),
            )
            self.status_message = "Settings saved"
            self.view = "main"

    def _handle_actions_key(self, key: int) -> None:
        target = self._selected_target()
        choices = self._target_actions(target)
        if key in (ord("q"), 27):
            self.view = "main"
        elif key in (curses.KEY_UP, ord("k")):
            self.action_selected = (self.action_selected - 1) % len(choices)
        elif key in (curses.KEY_DOWN, ord("j")):
            self.action_selected = (self.action_selected + 1) % len(choices)
        elif key in (10, 13, curses.KEY_ENTER):
            choice = choices[self.action_selected]
            self.view = "targets"
            if choice == "Run foreground":
                self._run_selected()
            elif choice == "Queue background":
                self._queue_selected()
            elif choice == "Profile":
                self._start_command(["profile", target.name], "Profile target")
            elif choice == "Information":
                self._start_command(["info", target.name], "Target information")
            elif choice == "Why rebuild":
                self._start_command(["why", target.name], "Rebuild explanation")
            elif choice == "Generate":
                self._start_command(["gen", target.name], "Generate target")
            elif choice == "Run test":
                self._start_command(["test", target.name.removeprefix("test-")], "Run test")

    def _handle_tests_key(self, key: int) -> None:
        if key in (ord("q"), 27):
            self.view = "main"
        elif key in (curses.KEY_UP, ord("k")):
            self.test_selected = (self.test_selected - 1) % len(TEST_ITEMS)
        elif key in (curses.KEY_DOWN, ord("j")):
            self.test_selected = (self.test_selected + 1) % len(TEST_ITEMS)
        elif key in (10, 13, curses.KEY_ENTER):
            item = TEST_ITEMS[self.test_selected]
            self.view = "main"
            self._start_command(["test", item], f"Run test {item}")

    def _filtered_targets(self) -> list[str]:
        graph = self._graph()
        if graph is None:
            return []
        needle = self.filter_text.casefold()
        cache_key = (id(graph), needle, len(graph.targets))
        if cache_key == self._filtered_cache_key:
            return list(self._filtered_cache)
        names = [
            name
            for name in sorted(graph.targets)
            if not needle or needle in name.casefold() or needle in graph.targets[name].kind.casefold()
        ]
        self._filtered_cache_key = cache_key
        self._filtered_cache = names
        return list(names)

    def _available_presets(self) -> list[tuple[str, str]]:
        return list(BUILD_PRESETS)

    def _start_preset(self, *, background: bool) -> None:
        presets = self._available_presets()
        if not presets:
            self.status_message = "No build presets are available"
            return
        name, label = presets[self.build_selected]
        if name == "clean" and not self._confirm("Clean generated build artifacts?"):
            return
        if name == "menuconfig":
            if background:
                self.status_message = "Configuration editor must run in the foreground"
            else:
                self._run_menuconfig()
            return
        if background:
            self._start_command(["client", "run", name], f"Queue {label}")
        else:
            self._start_command(["run", name], f"Build {label}")

    def _selected_target(self) -> Target:
        names = self._filtered_targets()
        if not names:
            raise BuildFailure("no target matches the current filter")
        graph = self._graph()
        if graph is None:
            raise BuildFailure("target graph is still loading")
        self.selected = min(self.selected, len(names) - 1)
        return graph.targets[names[self.selected]]

    def _target_actions(self, target: Target) -> list[str]:
        choices = [
            "Run foreground",
            "Queue background",
            "Profile",
            "Information",
            "Why rebuild",
            "Generate",
        ]
        if target.name.startswith("test-") and target.name.removeprefix("test-") in TEST_ITEMS:
            choices.append("Run test")
        return choices

    def _run_selected(self) -> None:
        target = self._selected_target()
        if target.name == "clean" and not self._confirm("Run clean and delete generated build artifacts?"):
            return
        if target.name == "menuconfig":
            self._run_menuconfig()
            return
        self._start_command(["run", target.name], f"Run {target.name}")

    def _queue_selected(self) -> None:
        target = self._selected_target()
        if target.name == "menuconfig":
            self.status_message = "menuconfig requires an interactive foreground terminal"
            return
        self._start_command(["client", "run", target.name], f"Queue {target.name}")

    def _start_command(self, args: list[str], title: str) -> None:
        if self.job is not None:
            self.status_message = "A command is already running; Ctrl-C requests termination"
            return
        command = [sys.executable, "build.py", "--json", *args]
        self.output_lines.clear()
        self.output_lines.append("$ " + " ".join(shlex.quote(part) for part in command))
        self.output_title = title
        self.output_follow = True
        self.text_scroll = 0
        try:
            self.job = CommandJob(command, args, self.paths.root)
        except OSError as error:
            self.output_lines.append(f"Unable to start command: {error}")
            self.status_message = "Unable to start command"
            self.exit_code = 1
        self.previous_view = "main"
        self.view = "output"

    def _execute_command(self, value: str) -> None:
        try:
            args = shlex.split(value)
        except ValueError as error:
            self.status_message = f"Invalid command: {error}"
            return
        if not args:
            return
        command = args[0]
        if command in {"q", "quit", "exit"}:
            self.running = False
        elif command == "help":
            self._show_lines("TUI help", self._help_lines())
        elif command == "map":
            self._open_dependency_map()
        elif command == "settings":
            self._open_settings()
        elif command == "status" and len(args) == 2:
            self._show_task_status(args[1])
        elif command == "log" and len(args) == 2:
            self._open_task_log(args[1])
        elif command == "cache" and len(args) == 2 and args[1] == "prune":
            if self._confirm("Prune stale cache state and temporary files?"):
                self._start_command(args, "Cache prune")
        elif command == "run" and len(args) >= 2 and args[1] == "clean":
            if self._confirm("Run clean and delete generated build artifacts?"):
                self._start_command(args, "Run clean")
        elif command == "run" and len(args) >= 2 and args[1] == "menuconfig":
            self._run_menuconfig()
        elif command == "tui":
            self.status_message = "The TUI is already running"
        else:
            self._start_command(args, "build.py " + " ".join(args))

    def _show_lines(self, title: str, lines: list[str]) -> None:
        self.viewer_title = title
        self.viewer_lines = lines or ["(no output)"]
        self.text_scroll = 0
        self.previous_view = "main"
        self.view = "viewer"

    def _open_dependency_map(self) -> None:
        graph = self._graph()
        if graph is None:
            if self.graph_state == "failed":
                self.status_message = f"Unable to load target graph: {self.graph_error}"
            else:
                self.status_message = "Target graph is loading; map will be available shortly"
            return
        self._show_lines("Dependency map", graph.map_lines())

    def _open_settings(self) -> None:
        settings = load_settings(self.paths)
        self.settings_values = [
            settings.worker_threads,
            settings.max_processes,
            settings.download_retries,
        ]
        self.settings_selected = 0
        self.view = "settings"

    def _refresh_recent_tasks(self) -> None:
        self._recent_records = self._read_task_records(force=True)
        self.task_selected = min(self.task_selected, max(0, len(self._recent_records) - 1))
        self.status_message = f"Loaded {len(self._recent_records)} task records"

    def _selected_task_id(self) -> str:
        return str(self._recent_records[self.task_selected].get("id", ""))

    def _show_task_status(self, task_id: str) -> None:
        try:
            value = self.store.read(task_id)
        except (FileNotFoundError, ValueError) as error:
            self.status_message = str(error)
            return
        self._show_lines(f"Task {task_id} status", json.dumps(value, ensure_ascii=False, indent=2).splitlines())

    def _open_task_log(self, task_id: str) -> None:
        try:
            record = self.store.read(task_id)
            raw_path = record.get("log")
            if not isinstance(raw_path, str):
                raise BuildFailure(f"task {task_id} does not record a log path")
            log_path = (self.paths.root / raw_path).resolve()
            log_path.relative_to(self.paths.root.resolve())
            lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except (BuildFailure, FileNotFoundError, OSError, ValueError) as error:
            self.status_message = str(error)
            return
        self._show_lines(f"Task {task_id} log", lines or ["(empty log)"])

    def _run_menuconfig(self) -> None:
        assert self.screen is not None
        self.status_message = "Starting menuconfig in the terminal"
        self.screen.erase()
        self.screen.refresh()
        try:
            curses.def_prog_mode()
            curses.endwin()
            result = subprocess.run(
                [sys.executable, "build.py", "run", "menuconfig"],
                cwd=self.paths.root,
                check=False,
            )
            self.status_message = "menuconfig completed" if result.returncode == 0 else f"menuconfig failed with exit {result.returncode}"
            if result.returncode:
                self.exit_code = 1
        except OSError as error:
            self.status_message = f"Unable to start menuconfig: {error}"
            self.exit_code = 1
        finally:
            curses.reset_prog_mode()
            self.screen.keypad(True)
            self.screen.nodelay(True)
            self.screen.clear()
            self.screen.refresh()

    def _help_lines(self) -> list[str]:
        return [
            "This is an independent ncurses frontend for build.py.",
            "",
            "Dashboard",
            "  Up/Down: choose a common build target",
            "  Enter: start a foreground build",
            "  b: queue a background build",
            "  o: open the live monitor for the current task",
            "  e: open the full target explorer",
            "",
            "Target explorer",
            "  Up/Down: select a graph target",
            "  Enter: choose an action for the selected target",
            "  /: filter target names and kinds",
            "  r: foreground run    b: queue background client run",
            "  p: profile    i: info    w: why rebuild    g: gen",
            "  a: affected query    x: select a test",
            "",
            "Views",
            "  m: dependency map    t: task history and logs",
            "  c: cache stats    C: confirmed cache prune",
            "  s: scheduler settings",
            "",
            "Command palette",
            "  : accepts the normal build.py commands, including:",
            "  run <target>, gen <file-or-target>, test <item>, profile <target>,",
            "  info <subject>, why <subject>, affected <file>, cache <stats|prune>,",
            "  client <run|gen|test|profile> ..., status <task-id>, log <task-id>.",
            "  run menuconfig temporarily restores the terminal for the Kconfig UI.",
            "",
            "Output",
            "  Ctrl-C asks the active foreground child process to terminate.",
            "  Text views use Up/Down, PageUp/PageDown, g, and G to scroll.",
        ]

    def _prompt(self, label: str) -> str | None:
        assert self.screen is not None
        rows, cols = self._size()
        prompt = f"{label}: "
        try:
            self.screen.timeout(-1)
            curses.curs_set(1)
            self._add(rows - 2, 1, prompt, curses.A_REVERSE, cols - 2)
            self.screen.move(rows - 2, min(cols - 2, len(prompt) + 1))
            self.screen.clrtoeol()
            raw = self.screen.getstr(rows - 2, min(cols - 2, len(prompt) + 1), max(1, cols - len(prompt) - 3))
            return raw.decode("utf-8", errors="replace")
        except curses.error:
            return None
        finally:
            try:
                curses.curs_set(0)
            except curses.error:
                pass
            self.screen.nodelay(True)

    def _confirm(self, message: str) -> bool:
        assert self.screen is not None
        rows, cols = self._size()
        width = min(max(32, len(message) + 8), cols - 4)
        left = max(2, (cols - width) // 2)
        top = max(3, rows // 2 - 2)
        for y in range(top, min(rows - 1, top + 5)):
            self._add(y, left, " " * width, curses.A_REVERSE, width)
        self._add(top + 1, left + 2, message, curses.A_REVERSE, width - 4)
        self._add(top + 3, left + 2, "Press y to confirm, any other key to cancel.", curses.A_REVERSE, width - 4)
        self.screen.refresh()
        self.screen.timeout(-1)
        try:
            return self.screen.getch() in (ord("y"), ord("Y"))
        finally:
            self.screen.nodelay(True)

    def _box(self, top: int, left: int, width: int, height: int, title: str) -> None:
        if width < 4 or height < 3:
            return
        horizontal = "-" * max(1, width - 2)
        self._add(top, left, "+" + horizontal + "+", self._color(1), width)
        self._add(top + height - 1, left, "+" + horizontal + "+", self._color(1), width)
        for row in range(top + 1, top + height - 1):
            self._add(row, left, "|", self._color(1), 1)
            self._add(row, left + width - 1, "|", self._color(1), 1)
        self._add(top, left + 2, f" {title} ", curses.A_BOLD | self._color(1), width - 4)

    @staticmethod
    def _preset_kind(name: str) -> str:
        if name in {"run", "run-debug", "run-iso"}:
            return "QEMU"
        if name in {"clean", "menuconfig"}:
            return "TOOL"
        if name.startswith("image") or name == "installer":
            return "IMAGE"
        return "BUILD"

    @staticmethod
    def _as_int(value: object) -> int:
        try:
            return int(value)
        except (TypeError, ValueError):
            return 0

    def _add(self, y: int, x: int, text: str, attribute: int = 0, width: int | None = None) -> None:
        assert self.screen is not None
        rows, cols = self.screen.getmaxyx()
        if y < 0 or y >= rows or x < 0 or x >= cols:
            return
        available = cols - x if width is None else min(width, cols - x)
        if available <= 0:
            return
        try:
            self.screen.addnstr(y, x, text, available, attribute)
        except curses.error:
            pass

    def _size(self) -> tuple[int, int]:
        assert self.screen is not None
        return self.screen.getmaxyx()


def run_tui(paths: BuildPaths, graph_loader: GraphLoader) -> int:
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        raise BuildFailure("tui requires an interactive TTY")
    app = BuildTui(paths, graph_loader)
    try:
        return curses.wrapper(app.run)
    except curses.error as error:
        raise BuildFailure(f"unable to initialize curses TUI: {error}") from error
