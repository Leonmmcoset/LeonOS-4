from __future__ import annotations

import hashlib
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
from concurrent.futures import FIRST_COMPLETED, Future, ThreadPoolExecutor, wait
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable

from .model import BuildGraph, Target
from .state import BuildPaths, TaskStore, atomic_json, strip_ansi, utc_now


RESET = "\x1b[0m"
CYAN = "\x1b[96m"
ORANGE = "\x1b[38;5;208m"
GREEN = "\x1b[92m"
YELLOW = "\x1b[93m"
RED = "\x1b[91m"
DIM = "\x1b[2m"
WHITE = "\x1b[97m"


def styled(color: str, text: str) -> str:
    return f"{color}{text}{RESET}"


class BuildFailure(RuntimeError):
    pass


class CommandError(BuildFailure):
    def __init__(self, command: Iterable[str], returncode: int) -> None:
        self.command = tuple(command)
        self.returncode = returncode
        super().__init__(f"command failed with exit {returncode}: {' '.join(self.command)}")


@dataclass(slots=True)
class BuildSettings:
    worker_threads: int
    max_processes: int
    download_retries: int = 3

    @classmethod
    def automatic(cls) -> "BuildSettings":
        count = max(1, os.cpu_count() or 1)
        return cls(worker_threads=count, max_processes=count)


@dataclass(slots=True)
class BuildMetrics:
    started_tasks: int = 0
    entered_folders: int = 0
    built_files: int = 0
    generated_files: int = 0
    ran_commands: int = 0
    downloaded_files: int = 0
    errors: int = 0
    completed: int = 0
    total: int = 0
    executed_targets: int = 0
    skipped_targets: int = 0
    worker_busy_seconds: float = 0.0
    peak_parallel_workers: int = 0
    elapsed_seconds: float = 0.0
    timings: list[dict[str, object]] = field(default_factory=list)

    def summary(self) -> list[str]:
        return [
            "Result:",
            f"Started {self.started_tasks} tasks",
            f"Entered {self.entered_folders} folders",
            f"Built {self.built_files} files",
            f"Generated {self.generated_files} files",
            f"Ran {self.ran_commands} commands",
            f"Downloaded {self.downloaded_files} files",
            f"{self.errors} errors",
        ]


class ProgressRenderer:
    """Render one safe progress line after the latest interactive log entry."""

    def __init__(self) -> None:
        self.enabled = sys.stdout.isatty()
        self._lock = threading.Lock()
        self._active = False
        self._progress_line: str | None = None
        self._visible = False

    def start(self) -> None:
        if not self.enabled:
            return
        with self._lock:
            if self._active:
                return
            self._active = True

    def write_log(self, line: str) -> None:
        with self._lock:
            self._clear_progress_locked()
            sys.stdout.write(line + "\n")
            if self._active:
                self._draw_progress_locked()
            sys.stdout.flush()

    def update(self, done: int, total: int, running: int) -> None:
        if not self.enabled:
            return
        with self._lock:
            width = max(12, min(40, shutil.get_terminal_size((100, 24)).columns - 38))
            ratio = 1.0 if total == 0 else min(1.0, done / total)
            fill = int(width * ratio)
            bar = "#" * fill + "-" * (width - fill)
            self._progress_line = f"{CYAN}[{bar}] {done}/{total} targets, {running} running{RESET}"
            self._clear_progress_locked()
            self._draw_progress_locked()
            sys.stdout.flush()

    def suspend(self) -> None:
        if not self.enabled:
            return
        with self._lock:
            self._active = False
            self._clear_progress_locked()
            sys.stdout.flush()

    def resume(self) -> None:
        if not self.enabled:
            return
        with self._lock:
            self._active = True
            self._draw_progress_locked()
            sys.stdout.flush()

    def _draw_progress_locked(self) -> None:
        if self._active and self._progress_line is not None:
            sys.stdout.write(f"\r\x1b[2K{self._progress_line}")
            self._visible = True

    def _clear_progress_locked(self) -> None:
        if self._visible:
            sys.stdout.write("\r\x1b[2K")
            self._visible = False

    def close(self) -> None:
        if not self.enabled:
            return
        with self._lock:
            if self._active:
                self._clear_progress_locked()
                sys.stdout.flush()
            self._active = False
            self._progress_line = None


class TaskLogger:
    def __init__(self, log_path: Path, metrics: BuildMetrics) -> None:
        self.log_path = log_path
        self.metrics = metrics
        self.progress = ProgressRenderer()
        self._lock = threading.Lock()
        self._entered: set[Path] = set()
        self._handle = log_path.open("w", encoding="utf-8", newline="\n")

    def close(self) -> None:
        self.progress.close()
        with self._lock:
            self._handle.close()

    def emit(self, text: str, color: str = WHITE) -> None:
        plain = strip_ansi(text)
        rendered = text if "\x1b[" in text else styled(color, plain)
        with self._lock:
            self._handle.write(plain + "\n")
            self._handle.flush()
        self.progress.write_log(rendered)

    def start_task(self, name: str, depth: int) -> None:
        self.metrics.started_tasks += 1
        self.emit(
            f"{styled(CYAN, '-- Start building task')} {styled(WHITE, name)} "
            f"{styled(CYAN, '--')} {styled(DIM, f'({depth})')}",
        )

    def start_run(self, name: str, task_id: str) -> None:
        self.metrics.started_tasks += 1
        self.emit(
            f"{styled(CYAN, '-- Start building task')} {styled(WHITE, name)} "
            f"{styled(CYAN, '--')} {styled(DIM, f'({task_id})')}",
        )

    def done_task(self, name: str, elapsed: float) -> None:
        self.emit(
            f"{styled(GREEN, 'DONE TASK')} {styled(WHITE, name)} "
            f"{styled(DIM, 'IN')} {styled(GREEN, f'{elapsed:.1f}s.')}",
        )

    def failed_task(self, name: str, error: BaseException) -> None:
        self.emit(
            f"{styled(RED, 'FAILED TASK')} {styled(WHITE, name)}: {styled(RED, str(error))}",
        )

    def enter_folder(self, path: Path, root: Path) -> None:
        try:
            relative = path.relative_to(root)
        except ValueError:
            relative = path
        if path in self._entered:
            return
        self._entered.add(path)
        self.metrics.entered_folders += 1
        self.emit(
            f"{styled(ORANGE, '-- Enter folder')} "
            f"{styled(WHITE, relative.as_posix() or '.')} {styled(ORANGE, '--')}",
        )

    def building(self, worker: int, target: Target, root: Path) -> None:
        label = target.source or (target.outputs[0] if target.outputs else root)
        self.enter_folder(label.parent, root)
        self.metrics.built_files += max(1, len(target.outputs))
        self.emit(
            f"{styled(CYAN, f'<{worker}>')} {styled(WHITE, 'Building')} "
            f"{styled(GREEN, root_relative(root, label))}",
        )

    def generating(self, worker: int, target: Target, root: Path) -> None:
        label = target.outputs[0] if target.outputs else Path(target.name)
        self.enter_folder(label.parent, root)
        self.metrics.generated_files += max(1, len(target.outputs))
        self.emit(
            f"{styled(CYAN, f'<{worker}>')} {styled(WHITE, 'Generating')} "
            f"{styled(YELLOW, root_relative(root, label))}",
        )

    def command(self, worker: int, command: Iterable[str]) -> None:
        quoted = " ".join(shell_quote(part) for part in command)
        self.metrics.ran_commands += 1
        self.emit(
            f"{styled(CYAN, f'<{worker}>')} {styled(WHITE, 'Running command:')} "
            f"{styled(DIM, f'\"{quoted}\"')}",
        )

    def download(self, worker: int, url: str, destination: Path, root: Path) -> None:
        self.metrics.downloaded_files += 1
        self.emit(
            f"{styled(CYAN, f'<{worker}>')} {styled(WHITE, 'Online downloading')} "
            f"{styled(CYAN, url)} {styled(WHITE, 'to')} "
            f"{styled(GREEN, root_relative(root, destination))}",
        )

    def child_output(self, text: str) -> None:
        if text:
            self.emit(text, DIM)


def shell_quote(value: str) -> str:
    if not value or any(char.isspace() or char in "'\"\\" for char in value):
        return json.dumps(value)
    return value


def root_relative(root: Path, path: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def normalized_path(root: Path, value: Path | str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = root / path
    return Path(os.path.normpath(path))


def parse_depfile(path: Path, root: Path) -> list[Path]:
    if not path.exists():
        return []
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    raw = raw.replace("\\\n", " ")
    if ":" not in raw:
        return []
    _, dependencies = raw.split(":", 1)
    words: list[str] = []
    current: list[str] = []
    escaped = False
    for char in dependencies:
        if escaped:
            current.append(char)
            escaped = False
        elif char == "\\":
            escaped = True
        elif char.isspace():
            if current:
                words.append("".join(current))
                current.clear()
        else:
            current.append(char)
    if current:
        words.append("".join(current))
    result: list[Path] = []
    for word in words:
        candidate = Path(word)
        if not candidate.is_absolute():
            candidate = root / candidate
        result.append(normalized_path(root, candidate))
    return result


@dataclass(slots=True)
class ActionContext:
    runner: "BuildRunner"
    target: Target
    worker_id: int

    @property
    def root(self) -> Path:
        return self.runner.paths.root

    @property
    def paths(self) -> BuildPaths:
        return self.runner.paths

    def run(
        self,
        command: Iterable[str],
        *,
        cwd: Path | None = None,
        environment: dict[str, str] | None = None,
        announce: bool = True,
        interactive: bool = False,
    ) -> None:
        self.runner.run_process(
            tuple(str(part) for part in command),
            self.worker_id,
            cwd=cwd,
            environment=environment,
            announce=announce,
            interactive=interactive,
        )

    def copy(self, source: Path, destination: Path) -> None:
        self.runner.logger.generating(self.worker_id, self.target, self.root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    def download(self, url: str, destination: Path) -> None:
        self.runner.download(url, destination, self.worker_id)


class BuildRunner:
    def __init__(
        self,
        graph: BuildGraph,
        paths: BuildPaths,
        settings: BuildSettings,
        task_id: str,
        *,
        engine_inputs: Iterable[Path] = (),
    ) -> None:
        self.graph = graph
        self.paths = paths
        self.settings = settings
        self.task_id = task_id
        self.store = TaskStore(paths)
        self.metrics = BuildMetrics()
        self.logger = TaskLogger(self.store.log_path(task_id), self.metrics)
        self.engine_inputs = tuple(engine_inputs)
        self._processes = threading.BoundedSemaphore(max(1, settings.max_processes))
        self._slots: queue.Queue[int] = queue.Queue()
        for index in range(max(1, settings.worker_threads)):
            self._slots.put(index)
        self._running = 0
        self._running_lock = threading.Lock()
        self._metrics_lock = threading.Lock()
        self._mtime_cache: dict[Path, int | None] = {}
        self._mtime_lock = threading.Lock()
        self._closed = False

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self.store.flush_target_states()
        finally:
            self.logger.close()

    def run(self, roots: Iterable[Target], command_name: str) -> BuildMetrics:
        roots = tuple(roots)
        closure = self.graph.closure(roots)
        self.metrics.total = len(closure)
        self.paths.ensure()
        self.store.target_states()
        self.store.update(
            self.task_id,
            status="running",
            started_at=utc_now(),
            task=command_name,
            total_targets=len(closure),
        )
        started = time.monotonic()
        try:
            self.logger.progress.start()
            self.logger.progress.update(0, len(closure), 0)
            self.logger.start_run(command_name, self.task_id)
            self._announce_groups(roots)
            self._schedule(closure)
            elapsed = time.monotonic() - started
            self.metrics.elapsed_seconds = round(elapsed, 3)
            self.store.flush_target_states()
            self.logger.done_task(command_name, elapsed)
            for line in self.metrics.summary():
                self.logger.emit(line, GREEN if line == "Result:" else WHITE)
            self.store.update(
                self.task_id,
                status="done",
                finished_at=utc_now(),
                elapsed_seconds=round(elapsed, 3),
                metrics=asdict(self.metrics),
            )
            return self.metrics
        except BaseException as exc:
            self.metrics.errors += 1
            elapsed = time.monotonic() - started
            self.metrics.elapsed_seconds = round(elapsed, 3)
            self.store.flush_target_states()
            self.logger.failed_task(command_name, exc)
            self.store.update(
                self.task_id,
                status="failed",
                finished_at=utc_now(),
                elapsed_seconds=round(elapsed, 3),
                error=str(exc),
                metrics=asdict(self.metrics),
            )
            if isinstance(exc, BuildFailure):
                raise
            raise BuildFailure(str(exc)) from exc
        finally:
            self.close()

    def profile_data(self, limit: int = 12) -> dict[str, object]:
        elapsed = self.metrics.elapsed_seconds
        capacity = max(1, self.settings.worker_threads)
        utilization = 0.0 if elapsed <= 0 else self.metrics.worker_busy_seconds / (elapsed * capacity)
        timings = sorted(
            self.metrics.timings,
            key=lambda timing: float(timing["seconds"]),
            reverse=True,
        )
        return {
            "elapsed_seconds": round(elapsed, 3),
            "executed_targets": self.metrics.executed_targets,
            "skipped_targets": self.metrics.skipped_targets,
            "worker_threads": capacity,
            "max_processes": self.settings.max_processes,
            "parallel": {
                "worker_busy_seconds": round(self.metrics.worker_busy_seconds, 3),
                "average_active_workers": round(
                    0.0 if elapsed <= 0 else self.metrics.worker_busy_seconds / elapsed,
                    3,
                ),
                "worker_utilization_percent": round(utilization * 100, 1),
                "peak_parallel_workers": self.metrics.peak_parallel_workers,
            },
            "slowest_targets": timings[:limit],
        }

    def _announce_groups(self, roots: Iterable[Target]) -> None:
        announced: set[str] = set()

        def visit(target: Target, depth: int) -> None:
            if target.name in announced:
                return
            announced.add(target.name)
            if target.group:
                self.logger.start_task(target.name, depth)
            for dependency in self.graph.dependencies(target):
                visit(dependency, depth + 1)

        for root in roots:
            visit(root, 0)

    def _schedule(self, closure: tuple[Target, ...]) -> None:
        names = {target.name: target for target in closure}
        dependencies = {
            target.name: {dependency.name for dependency in self.graph.dependencies(target) if dependency.name in names}
            for target in closure
        }
        dependents: dict[str, set[str]] = {name: set() for name in names}
        for name, required in dependencies.items():
            for dependency in required:
                dependents[dependency].add(name)

        ready = [name for name, required in dependencies.items() if not required]
        running: dict[Future[None], str] = {}
        failed: BaseException | None = None
        with ThreadPoolExecutor(max_workers=max(1, self.settings.worker_threads)) as pool:
            while ready or running:
                while ready and failed is None:
                    name = ready.pop()
                    future = pool.submit(self._execute, names[name])
                    running[future] = name
                self.logger.progress.update(self.metrics.completed, self.metrics.total, len(running))
                if not running:
                    break
                complete, _ = wait(running, return_when=FIRST_COMPLETED)
                for future in complete:
                    name = running.pop(future)
                    try:
                        future.result()
                    except BaseException as exc:
                        failed = exc
                        for pending in running:
                            pending.cancel()
                        break
                    self.metrics.completed += 1
                    for dependent in dependents[name]:
                        dependencies[dependent].discard(name)
                        if not dependencies[dependent]:
                            ready.append(dependent)
            if failed is not None:
                raise failed
        if self.metrics.completed != len(closure):
            raise BuildFailure("build graph did not complete")

    def _execute(self, target: Target) -> None:
        worker = self._slots.get()
        started = time.perf_counter()
        status = "aggregate"
        with self._running_lock:
            self._running += 1
            self.metrics.peak_parallel_workers = max(
                self.metrics.peak_parallel_workers,
                self._running,
            )
        try:
            if target.group:
                return
            if not self.rebuild_reasons(target):
                status = "skipped"
                with self._metrics_lock:
                    self.metrics.skipped_targets += 1
                return
            status = "executed"
            self._announce_target(worker, target)
            context = ActionContext(self, target, worker)
            if target.command is not None:
                for output in target.outputs:
                    output.parent.mkdir(parents=True, exist_ok=True)
                if target.depfile is not None:
                    target.depfile.parent.mkdir(parents=True, exist_ok=True)
                self.run_process(target.command, worker, cwd=target.cwd, environment=target.environment, announce=False)
            elif target.action is not None:
                target.action(context)
            else:
                raise BuildFailure(f"target {target.name} has no action")
            if target.outputs and any(not output.exists() for output in target.outputs):
                missing = next(output for output in target.outputs if not output.exists())
                raise BuildFailure(f"target {target.name} did not create {root_relative(self.paths.root, missing)}")
            self._refresh_mtimes((*target.outputs, target.depfile))
            self._write_target_state(target)
            with self._metrics_lock:
                self.metrics.executed_targets += 1
        finally:
            elapsed = time.perf_counter() - started
            if not target.group:
                with self._metrics_lock:
                    if status != "skipped":
                        self.metrics.worker_busy_seconds += elapsed
                    self.metrics.timings.append(
                        {
                            "target": target.name,
                            "kind": target.kind,
                            "worker": worker,
                            "status": status,
                            "seconds": round(elapsed, 4),
                        }
                    )
            with self._running_lock:
                self._running -= 1
            self._slots.put(worker)

    def _announce_target(self, worker: int, target: Target) -> None:
        if target.kind in {"compile", "assemble", "link"}:
            self.logger.building(worker, target, self.paths.root)
        elif target.kind == "download":
            return
        elif target.kind == "command":
            if target.command is not None:
                self.logger.command(worker, target.command)
        else:
            self.logger.generating(worker, target, self.paths.root)

    def _fingerprint(self, target: Target) -> str:
        payload = {
            "name": target.name,
            "kind": target.kind,
            "outputs": [root_relative(self.paths.root, path) for path in target.outputs],
            "inputs": [root_relative(self.paths.root, path) for path in target.all_inputs()],
            "depends_on": list(target.depends_on),
            "command": list(target.command or ()),
            "action": target.action_key,
            "depfile": root_relative(self.paths.root, target.depfile) if target.depfile else "",
            "environment": target.environment,
        }
        return hashlib.sha256(
            json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest()

    def _dependency_entries(self, target: Target, state: dict[str, object]) -> list[tuple[Path, str]]:
        dependencies = [(path, "input") for path in target.inputs]
        dependencies.extend((path, "implicit input") for path in target.implicit_inputs)
        for raw in state.get("depfile_dependencies", []):
            if not isinstance(raw, str):
                continue
            dependencies.append((normalized_path(self.paths.root, raw), "depfile dependency"))
        return dependencies

    def _mtime(self, path: Path) -> int | None:
        path = normalized_path(self.paths.root, path)
        with self._mtime_lock:
            if path in self._mtime_cache:
                return self._mtime_cache[path]
        try:
            value = path.stat().st_mtime_ns
        except OSError:
            value = None
        with self._mtime_lock:
            self._mtime_cache[path] = value
        return value

    def _refresh_mtimes(self, paths: Iterable[Path | None]) -> None:
        for path in paths:
            if path is None:
                continue
            normalized = normalized_path(self.paths.root, path)
            try:
                value = normalized.stat().st_mtime_ns
            except OSError:
                value = None
            with self._mtime_lock:
                self._mtime_cache[normalized] = value

    def rebuild_reasons(self, target: Target) -> list[str]:
        if target.always:
            return ["target is configured to always run"]
        if not target.outputs:
            if target.action is not None or target.command is not None:
                return ["target has no declared outputs"]
            return []
        missing_outputs = [output for output in target.outputs if self._mtime(output) is None]
        if missing_outputs:
            return [
                "missing output: " + root_relative(self.paths.root, output)
                for output in missing_outputs
            ]
        state = self.store.read_target(target.name)
        if state.get("fingerprint") != self._fingerprint(target):
            return ["command, action, or dependency fingerprint changed"]
        oldest_output = min(self._mtime(output) or 0 for output in target.outputs)
        reasons: list[str] = []
        seen: set[Path] = set()
        for dependency, kind in self._dependency_entries(target, state):
            if dependency in seen:
                continue
            seen.add(dependency)
            modified = self._mtime(dependency)
            relative = root_relative(self.paths.root, dependency)
            if modified is None:
                reasons.append(f"missing {kind}: {relative}")
            elif modified > oldest_output:
                reasons.append(f"newer {kind}: {relative}")
        return reasons

    def needs_rebuild(self, target: Target) -> bool:
        return bool(self.rebuild_reasons(target))

    def explain(self, target: Target) -> dict[str, object]:
        reasons = self.rebuild_reasons(target)
        return {
            "target": target.name,
            "kind": target.kind,
            "will_rebuild": bool(reasons),
            "outputs": [root_relative(self.paths.root, output) for output in target.outputs],
            "reasons": reasons,
        }

    def _write_target_state(self, target: Target) -> None:
        if not target.outputs:
            return
        depfile_dependencies: list[str] = []
        if target.depfile is not None:
            depfile_dependencies = [
                root_relative(self.paths.root, dependency)
                for dependency in parse_depfile(target.depfile, self.paths.root)
            ]
        self.store.write_target(
            target.name,
            {
                "target": target.name,
                "fingerprint": self._fingerprint(target),
                "updated_at": utc_now(),
                "depfile_dependencies": depfile_dependencies,
                "outputs": [root_relative(self.paths.root, output) for output in target.outputs],
            },
        )

    def run_process(
        self,
        command: tuple[str, ...],
        worker: int,
        *,
        cwd: Path | None = None,
        environment: dict[str, str] | None = None,
        announce: bool = True,
        interactive: bool = False,
    ) -> None:
        if announce:
            self.logger.command(worker, command)
        merged_environment = os.environ.copy()
        if environment:
            merged_environment.update(environment)
        if interactive:
            if not sys.stdin.isatty() or not sys.stdout.isatty() or not sys.stderr.isatty():
                raise BuildFailure("interactive command requires a TTY: " + " ".join(command))
            self.logger.progress.suspend()
            try:
                with self._processes:
                    process = subprocess.Popen(
                        command,
                        cwd=str(cwd or self.paths.root),
                        env=merged_environment,
                    )
                    returncode = process.wait()
            finally:
                self.logger.progress.resume()
        else:
            with self._processes:
                process = subprocess.Popen(
                    command,
                    cwd=str(cwd or self.paths.root),
                    env=merged_environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    bufsize=1,
                )
                assert process.stdout is not None
                for line in process.stdout:
                    self.logger.child_output(line.rstrip("\n"))
                returncode = process.wait()
        if returncode != 0:
            raise CommandError(command, returncode)

    def download(self, url: str, destination: Path, worker: int) -> None:
        destination.parent.mkdir(parents=True, exist_ok=True)
        attempts = max(0, self.settings.download_retries)
        for attempt in range(attempts + 1):
            self.logger.download(worker, url, destination, self.paths.root)
            temporary = destination.with_suffix(destination.suffix + ".download")
            try:
                with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
                temporary.replace(destination)
                return
            except BaseException:
                temporary.unlink(missing_ok=True)
                if attempt >= attempts:
                    raise
                time.sleep(2**attempt)
